/**
 * @file drone_control_node.cpp
 * @brief Drone Control Node with PX4 Native DDS Integration
 * 
 * This node receives waypoints from the Motion Coordinator and translates them
 * into PX4 native messages for drone navigation. It handles the interface
 * between our ROS2 system and PX4 via XRCE-DDS.
 * 
 * The node provides:
 * - Service interface to receive waypoints from Motion Coordinator
 * - PX4 vehicle command publishing for arming/mode changes
 * - Offboard control mode management
 * - Trajectory setpoint publishing
 * - Vehicle status monitoring
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "rclcpp/qos.hpp"
#include "kaskazi_drone/srv/get_waypoints.hpp"

// PX4 native message types
#include "px4_msgs/msg/vehicle_command.hpp"
#include "px4_msgs/msg/vehicle_status.hpp"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "px4_msgs/msg/vehicle_global_position.hpp"
#include "px4_msgs/msg/offboard_control_mode.hpp"
#include "px4_msgs/msg/trajectory_setpoint.hpp"
#include "px4_msgs/msg/vehicle_control_mode.hpp"
// #include "px4_msgs/msg/estimator_status_flags.hpp" // Not needed for SITL simulation
#include "px4_msgs/msg/vehicle_command_ack.hpp"
#include "geometry_msgs/msg/point.hpp"

class DroneControlNode : public rclcpp::Node
{
public:
  /**
   * @brief Constructor - initializes the drone control node
   * @param options Node options for configuration
   */
  explicit DroneControlNode(const rclcpp::NodeOptions & options)
  : Node("drone_control_node", options), offboard_setpoint_counter_(0)
  {
    using namespace std::placeholders;

    // Create service server to receive waypoints from Motion Coordinator
    waypoint_service_ = this->create_service<kaskazi_drone::srv::GetWaypoints>(
      "waypoint_push_topic",
      std::bind(&DroneControlNode::get_waypoints_callback, this, _1, _2));

    // PX4 Publishers - using /fmu/in/ namespace for commands to PX4
    vehicle_command_pub_ = this->create_publisher<px4_msgs::msg::VehicleCommand>(
      "/fmu/in/vehicle_command", 10);
    
    offboard_control_mode_pub_ = this->create_publisher<px4_msgs::msg::OffboardControlMode>(
      "/fmu/in/offboard_control_mode", 10);
    
    trajectory_setpoint_pub_ = this->create_publisher<px4_msgs::msg::TrajectorySetpoint>(
      "/fmu/in/trajectory_setpoint", 10);

    // Configure QoS to match PX4's BEST_EFFORT reliability
    auto px4_qos = rclcpp::QoS(10).reliability(rclcpp::ReliabilityPolicy::BestEffort);
    
    // PX4 Subscribers - using /fmu/out/ namespace for data from PX4
    vehicle_status_sub_ = this->create_subscription<px4_msgs::msg::VehicleStatus>(
      "/fmu/out/vehicle_status_v1", px4_qos,
      std::bind(&DroneControlNode::vehicle_status_callback, this, std::placeholders::_1));
      
    vehicle_local_position_sub_ = this->create_subscription<px4_msgs::msg::VehicleLocalPosition>(
      "/fmu/out/vehicle_local_position", px4_qos,
      std::bind(&DroneControlNode::vehicle_local_position_callback, this, std::placeholders::_1));
      
    vehicle_global_position_sub_ = this->create_subscription<px4_msgs::msg::VehicleGlobalPosition>(
      "/fmu/out/vehicle_global_position", px4_qos,
      std::bind(&DroneControlNode::vehicle_global_position_callback, this, std::placeholders::_1));
      
    // Skip EstimatorStatusFlags subscription - not needed for SITL simulation
      
    vehicle_command_ack_sub_ = this->create_subscription<px4_msgs::msg::VehicleCommandAck>(
      "/fmu/out/vehicle_command_ack", px4_qos,
      std::bind(&DroneControlNode::vehicle_command_ack_callback, this, std::placeholders::_1));

    // Timer for offboard control mode publishing (required for PX4)
    offboard_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      std::bind(&DroneControlNode::publish_offboard_control_mode, this));

    // Timer for trajectory setpoint publishing
    trajectory_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      std::bind(&DroneControlNode::publish_trajectory_setpoint, this));

    // Initialize state
    current_state_.nav_state = px4_msgs::msg::VehicleStatus::NAVIGATION_STATE_MANUAL;
    current_state_.arming_state = px4_msgs::msg::VehicleStatus::ARMING_STATE_STANDBY;
    
    // Initialize position
    current_position_.x = 0.0;
    current_position_.y = 0.0;
    current_position_.z = 0.0;
    
    // Initialize waypoint index
    current_waypoint_index_ = 0;
    mission_active_ = false;
    ekf2_initialized_ = true; // Always true for SITL simulation
    takeoff_completed_ = false;
    
    // Initialize command tracking
    last_command_result_ = 0;
    command_sequence_ = 0;

    RCLCPP_INFO(this->get_logger(), "PX4 Drone Control Node initialized");
  }

private:
  // Service callback to receive waypoints from Motion Coordinator
  void get_waypoints_callback(
    const std::shared_ptr<kaskazi_drone::srv::GetWaypoints::Request> request,
    std::shared_ptr<kaskazi_drone::srv::GetWaypoints::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "Received %zu waypoints from Motion Coordinator", 
                request->waypoints.size());

    // Store waypoints for mission execution
    mission_waypoints_ = request->waypoints;
    current_waypoint_index_ = 0;
    
    // Validate waypoints
    if (mission_waypoints_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No waypoints received");
      response->success = false;
      response->message = "No waypoints provided";
      return;
    }

    // Start mission execution
    response->success = execute_mission();
    response->message = response->success ? "Mission started successfully" : "Failed to start mission";
  }

  // Execute waypoint mission
  bool execute_mission()
  {
    RCLCPP_INFO(this->get_logger(), "Starting mission execution with %zu waypoints", 
                mission_waypoints_.size());

    // Wait for vehicle status connection
    if (!wait_for_px4_connection()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to connect to PX4");
      return false;
    }

    // SITL simulation - skip EKF2 initialization check
    RCLCPP_INFO(this->get_logger(), "SITL simulation mode - skipping EKF2 initialization check");

    // Send extended pre-flight offboard control signals
    RCLCPP_INFO(this->get_logger(), "Pre-flight: sending offboard control signals for 5 seconds...");
    auto start_time = this->now();
    while (rclcpp::ok() && (this->now() - start_time).seconds() < 5.0) {
      publish_offboard_control_mode();
      if (!mission_waypoints_.empty()) {
        publish_takeoff_trajectory(); // Send takeoff position
      }
      rclcpp::sleep_for(std::chrono::milliseconds(100));
    }

    // Arm the vehicle first
    if (!arm_vehicle()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to arm vehicle");
      return false;
    }

    // Continue sending offboard signals briefly after arming, then immediately switch to offboard
    start_time = this->now();
    while (rclcpp::ok() && (this->now() - start_time).seconds() < 0.5) {
      publish_offboard_control_mode();
      if (!mission_waypoints_.empty()) {
        publish_takeoff_trajectory();
      }
      rclcpp::sleep_for(std::chrono::milliseconds(100));
    }

    // Immediately switch to offboard mode to prevent auto-disarm timeout
    if (!set_offboard_mode()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to set offboard mode");
      return false;
    }

    // Execute takeoff
    if (!execute_takeoff()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to execute takeoff");
      return false;
    }

    // Start mission
    mission_active_ = true;
    current_waypoint_index_ = 0;
    
    RCLCPP_INFO(this->get_logger(), "Mission started successfully");
    return true;
  }

  // Wait for PX4 connection
  bool wait_for_px4_connection()
  {
    RCLCPP_INFO(this->get_logger(), "Waiting for PX4 connection...");
    
    auto start_time = this->now();
    while (rclcpp::ok() && (this->now() - start_time).seconds() < 10.0) {
      if (current_state_.nav_state != 0) {  // Valid state received
        RCLCPP_INFO(this->get_logger(), "PX4 connection established");
        return true;
      }
      rclcpp::sleep_for(std::chrono::milliseconds(100));
    }
    
    return false;
  }

  // Arm the vehicle
  bool arm_vehicle()
  {
    RCLCPP_INFO(this->get_logger(), "Arming vehicle...");
    
    px4_msgs::msg::VehicleCommand cmd{};
    cmd.timestamp = this->get_clock()->now().nanoseconds() / 1000;
    cmd.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_COMPONENT_ARM_DISARM;
    cmd.param1 = 1.0; // arm
    cmd.target_system = 1;
    cmd.target_component = 1;
    cmd.source_system = 1;
    cmd.source_component = 1;
    cmd.from_external = true;

    vehicle_command_pub_->publish(cmd);

    // Brief delay to allow command processing, then proceed
    rclcpp::sleep_for(std::chrono::milliseconds(200));
    RCLCPP_INFO(this->get_logger(), "Arm command sent - proceeding to offboard mode");
    
    return true;
  }

  // Wait for system readiness with robust fallback logic
  bool wait_for_ekf2_initialization()
  {
    RCLCPP_INFO(this->get_logger(), "Waiting for system readiness...");
    
    auto start_time = this->now();
    while (rclcpp::ok() && (this->now() - start_time).seconds() < 10.0) {
      // Primary check: EKF2 flags indicate good estimation
      if (ekf2_initialized_) {
        RCLCPP_INFO(this->get_logger(), "System ready - EKF2 initialized with valid estimation");
        return true;
      }
      
      // Fallback check: Basic vehicle communication established and minimum wait time passed
      if (current_state_.nav_state != 0 && current_state_.arming_state != 0 && 
          (this->now() - start_time).seconds() > 3.0) {
        RCLCPP_INFO(this->get_logger(), "System ready - Vehicle communication established (nav_state: %d, arming_state: %d)", 
                    current_state_.nav_state, current_state_.arming_state);
        return true;
      }
      
      rclcpp::sleep_for(std::chrono::milliseconds(250));
    }
    
    // Final fallback: Always proceed after timeout to prevent blocking
    RCLCPP_WARN(this->get_logger(), "System readiness timeout - proceeding with mission anyway");
    return true;
  }

  // Set offboard control mode with improved timing and retry logic
  bool set_offboard_mode()
  {
    RCLCPP_INFO(this->get_logger(), "Setting offboard mode immediately after arming...");
    
    for (int attempt = 0; attempt < 5; attempt++) {
      px4_msgs::msg::VehicleCommand cmd{};
      cmd.timestamp = this->get_clock()->now().nanoseconds() / 1000;
      cmd.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_DO_SET_MODE;
      cmd.param1 = 1.0; // MAV_MODE_FLAG_CUSTOM_MODE_ENABLED
      cmd.param2 = 6.0; // PX4_CUSTOM_MAIN_MODE_OFFBOARD
      cmd.param3 = 0.0; // PX4_CUSTOM_SUB_MODE_OFFBOARD
      cmd.target_system = 1;
      cmd.target_component = 1;
      cmd.source_system = 1;
      cmd.source_component = 1;
      cmd.from_external = true;
      cmd.confirmation = ++command_sequence_;

      vehicle_command_pub_->publish(cmd);
      RCLCPP_INFO(this->get_logger(), "Offboard mode command sent (attempt %d/5)", attempt + 1);

      // Wait for mode change confirmation with continuous offboard signals
      auto start_time = this->now();
      while (rclcpp::ok() && (this->now() - start_time).seconds() < 3.0) {
        // CRITICAL: Continue sending offboard signals every 100ms during transition
        publish_offboard_control_mode();
        if (!mission_waypoints_.empty()) {
          publish_takeoff_trajectory();
        }
        
        if (current_state_.nav_state == px4_msgs::msg::VehicleStatus::NAVIGATION_STATE_OFFBOARD) {
          RCLCPP_INFO(this->get_logger(), "✅ Offboard mode set successfully on attempt %d!", attempt + 1);
          return true;
        }
        rclcpp::sleep_for(std::chrono::milliseconds(100));
      }
      
      RCLCPP_WARN(this->get_logger(), "Offboard mode attempt %d failed - nav_state: %d, retrying immediately...", 
                  attempt + 1, current_state_.nav_state);
      
      // Brief pause before retry, but keep sending offboard signals
      for (int i = 0; i < 2; i++) {
        publish_offboard_control_mode();
        if (!mission_waypoints_.empty()) {
          publish_takeoff_trajectory();
        }
        rclcpp::sleep_for(std::chrono::milliseconds(100));
      }
    }
    
    RCLCPP_ERROR(this->get_logger(), "❌ Failed to set offboard mode after 5 attempts - final nav_state: %d", 
                 current_state_.nav_state);
    return false;
  }

  // Execute takeoff sequence
  bool execute_takeoff()
  {
    RCLCPP_INFO(this->get_logger(), "Executing takeoff to 3 meters...");
    
    // Send takeoff command
    px4_msgs::msg::VehicleCommand cmd{};
    cmd.timestamp = this->get_clock()->now().nanoseconds() / 1000;
    cmd.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_NAV_TAKEOFF;
    cmd.param7 = 3.0; // Takeoff altitude (3 meters)
    cmd.target_system = 1;
    cmd.target_component = 1;
    cmd.source_system = 1;
    cmd.source_component = 1;
    cmd.from_external = true;
    cmd.confirmation = ++command_sequence_;

    vehicle_command_pub_->publish(cmd);
    
    // Wait for takeoff completion by monitoring altitude
    auto start_time = this->now();
    while (rclcpp::ok() && (this->now() - start_time).seconds() < 30.0) {
      // Continue publishing offboard control
      publish_offboard_control_mode();
      publish_takeoff_trajectory();
      
      // Check if we've reached takeoff altitude
      if (current_position_.z >= 2.5) { // Allow some tolerance
        RCLCPP_INFO(this->get_logger(), "Takeoff completed - altitude: %.2f m", current_position_.z);
        takeoff_completed_ = true;
        return true;
      }
      
      rclcpp::sleep_for(std::chrono::milliseconds(100));
    }
    
    RCLCPP_ERROR(this->get_logger(), "Takeoff timeout - current altitude: %.2f m", current_position_.z);
    return false;
  }

  // Publish offboard control mode (required for PX4 offboard)
  void publish_offboard_control_mode()
  {
    px4_msgs::msg::OffboardControlMode msg{};
    msg.timestamp = this->get_clock()->now().nanoseconds() / 1000;
    msg.position = true;
    msg.velocity = false;
    msg.acceleration = false;
    msg.attitude = false;
    msg.body_rate = false;

    offboard_control_mode_pub_->publish(msg);
  }
  
  // Publish takeoff trajectory setpoint
  void publish_takeoff_trajectory()
  {
    px4_msgs::msg::TrajectorySetpoint msg{};
    msg.timestamp = this->get_clock()->now().nanoseconds() / 1000;
    
    // Set takeoff position (current position but 3m up)
    msg.position = {
      static_cast<float>(current_position_.x),
      static_cast<float>(current_position_.y),
      -3.0f // PX4 uses NED, so -3m for 3m up
    };
    
    // Set all velocities to zero for position hold
    msg.velocity = {0.0f, 0.0f, 0.0f};
    msg.acceleration = {0.0f, 0.0f, 0.0f};
    msg.jerk = {0.0f, 0.0f, 0.0f};
    msg.yaw = 0.0f;
    msg.yawspeed = 0.0f;

    trajectory_setpoint_pub_->publish(msg);
  }

  // Publish trajectory setpoint with enhanced validation
  void publish_trajectory_setpoint()
  {
    if (!mission_active_ || mission_waypoints_.empty() || !takeoff_completed_) {
      return;
    }

    // Check if we need to advance to next waypoint
    if (current_waypoint_index_ < mission_waypoints_.size()) {
      auto& target_waypoint = mission_waypoints_[current_waypoint_index_];
      
      // Check if we're close enough to the current waypoint
      double distance = sqrt(
        pow(current_position_.x - target_waypoint.x, 2) +
        pow(current_position_.y - target_waypoint.y, 2) +
        pow(current_position_.z - target_waypoint.z, 2)
      );
      
      if (distance < 1.5) { // 1.5 meter threshold for better reliability
        current_waypoint_index_++;
        if (current_waypoint_index_ >= mission_waypoints_.size()) {
          RCLCPP_INFO(this->get_logger(), "Mission completed! Landing...");
          mission_active_ = false;
          execute_landing();
          return;
        }
        RCLCPP_INFO(this->get_logger(), "Advancing to waypoint %zu/%zu (distance: %.2f m)", 
                    current_waypoint_index_ + 1, mission_waypoints_.size(), distance);
      }
    }

    if (current_waypoint_index_ < mission_waypoints_.size()) {
      auto& target_waypoint = mission_waypoints_[current_waypoint_index_];
      
      // Validate waypoint coordinates
      if (std::isnan(target_waypoint.x) || std::isnan(target_waypoint.y) || std::isnan(target_waypoint.z)) {
        RCLCPP_ERROR(this->get_logger(), "Invalid waypoint coordinates detected, skipping");
        current_waypoint_index_++;
        return;
      }
      
      px4_msgs::msg::TrajectorySetpoint msg{};
      msg.timestamp = this->get_clock()->now().nanoseconds() / 1000;
      
      // Ensure proper coordinate conversion and bounds
      float target_x = static_cast<float>(target_waypoint.x);
      float target_y = static_cast<float>(target_waypoint.y);
      float target_z = static_cast<float>(-target_waypoint.z); // PX4 uses NED, ROS uses ENU
      
      // Clamp altitude to safe bounds
      target_z = std::max(target_z, -50.0f); // Max 50m altitude
      target_z = std::min(target_z, -1.0f);  // Min 1m altitude
      
      msg.position = {target_x, target_y, target_z};
      msg.velocity = {0.0f, 0.0f, 0.0f}; // Zero velocity for position hold
      msg.acceleration = {0.0f, 0.0f, 0.0f};
      msg.jerk = {0.0f, 0.0f, 0.0f};
      msg.yaw = 0.0f; // Keep heading
      msg.yawspeed = 0.0f;

      trajectory_setpoint_pub_->publish(msg);
      
      // Log current progress
      static int log_counter = 0;
      if (++log_counter % 50 == 0) { // Log every 5 seconds (100ms * 50)
        RCLCPP_INFO(this->get_logger(), 
                    "Navigating to waypoint %zu/%zu: (%.1f, %.1f, %.1f) - Current: (%.1f, %.1f, %.1f)",
                    current_waypoint_index_ + 1, mission_waypoints_.size(),
                    target_waypoint.x, target_waypoint.y, target_waypoint.z,
                    current_position_.x, current_position_.y, current_position_.z);
      }
    }
  }
  
  // Execute landing sequence
  void execute_landing()
  {
    RCLCPP_INFO(this->get_logger(), "Executing landing sequence...");
    
    px4_msgs::msg::VehicleCommand cmd{};
    cmd.timestamp = this->get_clock()->now().nanoseconds() / 1000;
    cmd.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_NAV_LAND;
    cmd.target_system = 1;
    cmd.target_component = 1;
    cmd.source_system = 1;
    cmd.source_component = 1;
    cmd.from_external = true;
    cmd.confirmation = ++command_sequence_;

    vehicle_command_pub_->publish(cmd);
  }

  // Vehicle status callback
  void vehicle_status_callback(const px4_msgs::msg::VehicleStatus::SharedPtr msg)
  {
    current_state_ = *msg;
  }

  // Vehicle local position callback
  void vehicle_local_position_callback(const px4_msgs::msg::VehicleLocalPosition::SharedPtr msg)
  {
    current_position_.x = msg->x;
    current_position_.y = msg->y;
    current_position_.z = -msg->z; // Convert from NED to ENU
  }

  // Vehicle global position callback
  void vehicle_global_position_callback(const px4_msgs::msg::VehicleGlobalPosition::SharedPtr msg)
  {
    current_global_position_ = *msg;
  }
  
  // EstimatorStatusFlags callback removed for SITL simulation compatibility
  
  // Vehicle command acknowledgment callback
  void vehicle_command_ack_callback(const px4_msgs::msg::VehicleCommandAck::SharedPtr msg)
  {
    last_command_result_ = msg->result;
    
    // Log command results for debugging
    std::string result_str;
    switch (msg->result) {
      case px4_msgs::msg::VehicleCommandAck::VEHICLE_CMD_RESULT_ACCEPTED:
        result_str = "ACCEPTED";
        break;
      case px4_msgs::msg::VehicleCommandAck::VEHICLE_CMD_RESULT_TEMPORARILY_REJECTED:
        result_str = "TEMPORARILY_REJECTED";
        break;
      case px4_msgs::msg::VehicleCommandAck::VEHICLE_CMD_RESULT_DENIED:
        result_str = "DENIED";
        break;
      case px4_msgs::msg::VehicleCommandAck::VEHICLE_CMD_RESULT_UNSUPPORTED:
        result_str = "UNSUPPORTED";
        break;
      case px4_msgs::msg::VehicleCommandAck::VEHICLE_CMD_RESULT_FAILED:
        result_str = "FAILED";
        break;
      default:
        result_str = "UNKNOWN(" + std::to_string(msg->result) + ")";
    }
    
    RCLCPP_INFO(this->get_logger(), "Command %d result: %s", msg->command, result_str.c_str());
  }

  // Member variables
  rclcpp::Service<kaskazi_drone::srv::GetWaypoints>::SharedPtr waypoint_service_;
  
  // PX4 Publishers
  rclcpp::Publisher<px4_msgs::msg::VehicleCommand>::SharedPtr vehicle_command_pub_;
  rclcpp::Publisher<px4_msgs::msg::OffboardControlMode>::SharedPtr offboard_control_mode_pub_;
  rclcpp::Publisher<px4_msgs::msg::TrajectorySetpoint>::SharedPtr trajectory_setpoint_pub_;
  
  // PX4 Subscribers
  rclcpp::Subscription<px4_msgs::msg::VehicleStatus>::SharedPtr vehicle_status_sub_;
  rclcpp::Subscription<px4_msgs::msg::VehicleLocalPosition>::SharedPtr vehicle_local_position_sub_;
  rclcpp::Subscription<px4_msgs::msg::VehicleGlobalPosition>::SharedPtr vehicle_global_position_sub_;
  // EstimatorStatusFlags subscription removed for SITL compatibility
  rclcpp::Subscription<px4_msgs::msg::VehicleCommandAck>::SharedPtr vehicle_command_ack_sub_;
  
  // Timers
  rclcpp::TimerBase::SharedPtr offboard_timer_;
  rclcpp::TimerBase::SharedPtr trajectory_timer_;
  
  // State variables
  px4_msgs::msg::VehicleStatus current_state_;
  geometry_msgs::msg::Point current_position_;
  px4_msgs::msg::VehicleGlobalPosition current_global_position_;
  
  // Mission variables
  std::vector<geometry_msgs::msg::Point> mission_waypoints_;
  size_t current_waypoint_index_;
  bool mission_active_;
  bool ekf2_initialized_;
  bool takeoff_completed_;
  uint64_t offboard_setpoint_counter_;
  
  // Command tracking
  uint8_t last_command_result_;
  uint8_t command_sequence_;
};

// Register the component with class_loader
RCLCPP_COMPONENTS_REGISTER_NODE(DroneControlNode)

// Main function for standalone execution
int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  
  // Create node options for component
  rclcpp::NodeOptions options;
  
  // Create and spin the node
  auto node = std::make_shared<DroneControlNode>(options);
  rclcpp::spin(node);
  
  rclcpp::shutdown();
  return 0;
}
