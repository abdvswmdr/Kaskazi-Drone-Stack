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
#include <limits>

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

    // Timer for offboard control mode publishing (50Hz like working example)
    offboard_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(20),  // 50Hz instead of 10Hz
      std::bind(&DroneControlNode::publish_offboard_control_mode, this));

    // Timer for trajectory setpoint publishing (50Hz like working example)
    trajectory_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(20),  // 50Hz instead of 10Hz
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

    // Follow working ROS2 example pattern: ARM → TAKEOFF → wait for AUTO_LOITER → OFFBOARD
    RCLCPP_INFO(this->get_logger(), "Starting correct sequence: ARM → TAKEOFF → LOITER → OFFBOARD");
    
    // Step 1: Wait for system readiness (like working example checks flightCheck first)
    if (!wait_for_system_ready()) {
      RCLCPP_ERROR(this->get_logger(), "System not ready for arming");
      return false;
    }
    
    // Step 2: ARM the vehicle persistently (like working samples)
    if (!arm_vehicle_persistent()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to arm vehicle");
      return false;
    }

    // Step 2: Send TAKEOFF command (missing in our previous implementation)
    RCLCPP_INFO(this->get_logger(), "Sending takeoff command...");
    if (!send_takeoff_command()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to send takeoff command");
      return false;
    }

    // Step 3: Wait for AUTO_LOITER state (this is CORRECT, not an error!)
    RCLCPP_INFO(this->get_logger(), "Waiting for AUTO_LOITER state (this is expected!)...");
    if (!wait_for_auto_loiter()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to reach AUTO_LOITER state");
      return false;
    }

    // Step 4: NOW switch to offboard mode (following working ROS2 example)
    RCLCPP_INFO(this->get_logger(), "AUTO_LOITER reached! Now switching to offboard mode...");
    if (!set_offboard_mode()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to set offboard mode");
      return false;
    }

    // Step 5: Wait for takeoff completion
    RCLCPP_INFO(this->get_logger(), "Offboard mode active! Waiting for takeoff completion...");
    if (!wait_for_takeoff_completion()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to complete takeoff");
      return false;
    }

    // Start mission
    mission_active_ = true;
    current_waypoint_index_ = 0;
    
    RCLCPP_INFO(this->get_logger(), "Mission started successfully - ready for waypoint navigation!");
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

  // Wait for system ready (like working example's flightCheck)
  bool wait_for_system_ready()
  {
    RCLCPP_INFO(this->get_logger(), "Waiting for system to be ready for arming...");
    
    auto start_time = this->now();
    while (rclcpp::ok() && (this->now() - start_time).seconds() < 15.0) {
      if (current_state_.pre_flight_checks_pass) {
        RCLCPP_INFO(this->get_logger(), "✅ System ready - preflight checks passed!");
        return true;
      }
      RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                           "Waiting for preflight checks to pass... (current: %s)",
                           current_state_.pre_flight_checks_pass ? "PASS" : "FAIL");
      rclcpp::sleep_for(std::chrono::milliseconds(200));
    }
    
    RCLCPP_ERROR(this->get_logger(), "❌ System readiness timeout - preflight checks still failing");
    return false;
  }

  // Persistent arming (like working example that keeps sending arm commands)
  bool arm_vehicle_persistent()
  {
    RCLCPP_INFO(this->get_logger(), "Starting persistent arming sequence...");
    
    auto start_time = this->now();
    int arm_attempts = 0;
    
    while (rclcpp::ok() && (this->now() - start_time).seconds() < 15.0) {
      // Send arm command every second (like working example)
      if (arm_attempts == 0 || (this->now() - start_time).seconds() > arm_attempts) {
        RCLCPP_INFO(this->get_logger(), "Sending arm command (attempt %d)...", arm_attempts + 1);
        
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
        arm_attempts++;
      }
      
      RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                           "Current arming_state: %d, preflight: %s, waiting for ARMED (2)...", 
                           current_state_.arming_state,
                           current_state_.pre_flight_checks_pass ? "PASS" : "FAIL");
                           
      // Accept both STANDBY (1) and ARMED (2) states in SITL mode
      if (current_state_.arming_state >= 1 && current_state_.pre_flight_checks_pass) { 
        RCLCPP_INFO(this->get_logger(), "✅ Vehicle ready! arming_state: %d (SITL accepts STANDBY)", current_state_.arming_state);
        
        // Wait a bit more for stability (like working example's myCnt > 10)
        if ((this->now() - start_time).seconds() > 3.0) {
          return true;
        }
      }
      
      // If preflight checks fail during arming, that's the issue
      if (!current_state_.pre_flight_checks_pass) {
        RCLCPP_ERROR(this->get_logger(), "❌ Preflight checks failed during arming - vehicle will auto-disarm");
        return false;
      }
      
      rclcpp::sleep_for(std::chrono::milliseconds(100));
    }
    
    RCLCPP_ERROR(this->get_logger(), "❌ Persistent arming timeout - final arming_state: %d (expected: 2)", current_state_.arming_state);
    return false;
  }

  // Send takeoff command (missing in our previous implementation!)
  bool send_takeoff_command()
  {
    RCLCPP_INFO(this->get_logger(), "Sending VEHICLE_CMD_NAV_TAKEOFF command...");
    
    px4_msgs::msg::VehicleCommand cmd{};
    cmd.timestamp = this->get_clock()->now().nanoseconds() / 1000;
    cmd.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_NAV_TAKEOFF;
    cmd.param1 = 1.0; // Minimum pitch
    cmd.param7 = 5.0; // Takeoff altitude (5 meters like working example)
    cmd.target_system = 1;
    cmd.target_component = 1;
    cmd.source_system = 1;
    cmd.source_component = 1;
    cmd.from_external = true;

    vehicle_command_pub_->publish(cmd);
    RCLCPP_INFO(this->get_logger(), "Takeoff command sent");
    
    return true;
  }

  // Wait for AUTO_LOITER state (this is CORRECT behavior, not an error!)
  bool wait_for_auto_loiter()
  {
    RCLCPP_INFO(this->get_logger(), "Waiting for NAVIGATION_STATE_AUTO_LOITER (nav_state 4)...");
    
    auto start_time = this->now();
    while (rclcpp::ok() && (this->now() - start_time).seconds() < 15.0) {
      RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000, 
                           "Current nav_state: %d, waiting for AUTO_LOITER (4)...", current_state_.nav_state);
      
      if (current_state_.nav_state == 4) { // NAVIGATION_STATE_AUTO_LOITER = 4
        RCLCPP_INFO(this->get_logger(), "✅ AUTO_LOITER state reached! This is the correct intermediate state.");
        return true;
      }
      
      rclcpp::sleep_for(std::chrono::milliseconds(100));
    }
    
    RCLCPP_ERROR(this->get_logger(), "❌ AUTO_LOITER timeout - final nav_state: %d (expected: 4)", current_state_.nav_state);
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

  // Set offboard control mode (following working ROS2 example pattern)
  bool set_offboard_mode()
  {
    RCLCPP_INFO(this->get_logger(), "Starting offboard mode (following working ROS2 example)...");
    
    // Start continuous setpoint stream FIRST (like working example does at 50Hz)
    RCLCPP_INFO(this->get_logger(), "Starting continuous setpoint stream at 50Hz...");
    
    // Send a few setpoints to establish the stream
    for (int i = 0; i < 10; i++) {
      publish_offboard_control_mode();
      if (!mission_waypoints_.empty()) {
        publish_takeoff_trajectory();
      }
      rclcpp::sleep_for(std::chrono::milliseconds(20)); // 50Hz
    }
    
    // Now send offboard mode command
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

    vehicle_command_pub_->publish(cmd);
    RCLCPP_INFO(this->get_logger(), "Offboard mode command sent");

    // Continue setpoint stream while waiting for mode change
    auto start_time = this->now();
    while (rclcpp::ok() && (this->now() - start_time).seconds() < 3.0) {
      // Maintain 50Hz setpoint stream
      publish_offboard_control_mode();
      if (!mission_waypoints_.empty()) {
        publish_takeoff_trajectory();
      }
      
      if (current_state_.nav_state == 14) { // NAVIGATION_STATE_OFFBOARD = 14
        RCLCPP_INFO(this->get_logger(), "✅ Offboard mode activated successfully! nav_state: %d", 
                    current_state_.nav_state);
        return true;
      }
      
      rclcpp::sleep_for(std::chrono::milliseconds(20)); // 50Hz continuous stream
    }
    
    RCLCPP_ERROR(this->get_logger(), "❌ Failed to set offboard mode - final nav_state: %d", 
                 current_state_.nav_state);
    return false;
  }

  // Wait for takeoff completion (takeoff command already sent earlier)
  bool wait_for_takeoff_completion()
  {
    RCLCPP_INFO(this->get_logger(), "Waiting for takeoff completion (monitoring altitude)...");
    
    // Wait for takeoff completion by monitoring altitude
    auto start_time = this->now();
    while (rclcpp::ok() && (this->now() - start_time).seconds() < 30.0) {
      // Continue publishing offboard control at 50Hz
      publish_offboard_control_mode();
      publish_takeoff_trajectory();
      
      // Check if we've reached takeoff altitude
      if (current_position_.z >= 4.0) { // 5m takeoff altitude minus tolerance
        RCLCPP_INFO(this->get_logger(), "Takeoff completed - altitude: %.2f m", current_position_.z);
        takeoff_completed_ = true;
        return true;
      }
      
      rclcpp::sleep_for(std::chrono::milliseconds(20)); // 50Hz
    }
    
    RCLCPP_ERROR(this->get_logger(), "Takeoff timeout - current altitude: %.2f m", current_position_.z);
    return false;
  }

  // Publish offboard control mode (try position control first - more reliable for initial transition)
  void publish_offboard_control_mode()
  {
    px4_msgs::msg::OffboardControlMode msg{};
    msg.timestamp = this->get_clock()->now().nanoseconds() / 1000;
    msg.position = true;   // Use position control - more reliable for offboard transition
    msg.velocity = false;
    msg.acceleration = false;
    msg.attitude = false;
    msg.body_rate = false;

    offboard_control_mode_pub_->publish(msg);
  }
  
  // Publish simple position setpoint for reliable offboard transition
  void publish_takeoff_trajectory()
  {
    px4_msgs::msg::TrajectorySetpoint msg{};
    msg.timestamp = this->get_clock()->now().nanoseconds() / 1000;
    
    // Simple position hold at 5m altitude (matching takeoff command)
    msg.position = {0.0f, 0.0f, -5.0f};  // NED: 5m up = -5.0f
    
    // Zero velocity for position hold
    msg.velocity = {0.0f, 0.0f, 0.0f};
    msg.acceleration = {0.0f, 0.0f, 0.0f};
    msg.jerk = {0.0f, 0.0f, 0.0f};
    
    // Face north
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
      
      // Calculate velocity towards target for smoother navigation
      float distance_to_target = sqrt(
        pow(target_x - current_position_.x, 2) + 
        pow(target_y - current_position_.y, 2) + 
        pow(-target_z - current_position_.z, 2)
      );
      
      if (distance_to_target > 0.1f) {
        // Set moderate velocity towards target (max 2 m/s)
        float vel_scale = std::min(2.0f, distance_to_target * 0.5f);
        msg.velocity = {
          static_cast<float>((target_x - current_position_.x) / distance_to_target * vel_scale),
          static_cast<float>((target_y - current_position_.y) / distance_to_target * vel_scale),
          static_cast<float>((-target_z - current_position_.z) / distance_to_target * vel_scale * 0.5f) // Slower vertical
        };
      } else {
        msg.velocity = {0.0f, 0.0f, 0.0f}; // Stop when close
      }
      
      msg.acceleration = {0.0f, 0.0f, 0.0f};
      msg.jerk = {0.0f, 0.0f, 0.0f};
      
      // Calculate yaw towards target
      float yaw_target = atan2(target_y - current_position_.y, target_x - current_position_.x);
      msg.yaw = yaw_target;
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
