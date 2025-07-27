/**
 * @file drone_control_node.cpp
 * @brief Drone Control Node with MAVROS Integration
 * 
 * This node receives waypoints from the Motion Coordinator and translates them
 * into MAVROS waypoint messages for drone navigation. It handles the interface
 * between our ROS2 system and the drone's flight controller via MAVROS.
 * 
 * The node provides:
 * - Service interface to receive waypoints from Motion Coordinator
 * - MAVROS waypoint push service integration
 * - Waypoint formatting and validation
 * - Drone mission management
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "kaskazi_drone/srv/get_waypoints.hpp"
#include "mavros_msgs/srv/waypoint_push.hpp"
#include "mavros_msgs/msg/waypoint.hpp"
#include "geometry_msgs/msg/point.hpp"

class DroneControlNode : public rclcpp::Node
{
public:
  /**
   * @brief Constructor - initializes the drone control node
   * @param options Node options for configuration
   */
  explicit DroneControlNode(const rclcpp::NodeOptions & options)
  : Node("drone_control_node", options)
  {
    using namespace std::placeholders;

    // Create service server to receive waypoints from Motion Coordinator
    waypoint_service_ = this->create_service<kaskazi_drone::srv::GetWaypoints>(
      "waypoint_push_topic",
      std::bind(&DroneControlNode::get_waypoints_callback, this, _1, _2));

    // Create client for MAVROS waypoint push service
    mavros_waypoint_client_ = this->create_client<mavros_msgs::srv::WaypointPush>(
      "/mavros/mission/push");

    // Wait for MAVROS waypoint service to be available
    while (!mavros_waypoint_client_->wait_for_service(std::chrono::seconds(1))) {
      if (!rclcpp::ok()) {
        RCLCPP_ERROR(this->get_logger(), "Interrupted while waiting for MAVROS waypoint service");
        return;
      }
      RCLCPP_INFO(this->get_logger(), "Waypoint Push service not available, looking for /mavros/mission/push");
    }

    // Initialize waypoint list
    waypoint_list_.clear();

    RCLCPP_INFO(this->get_logger(), "Drone Control Node initialized and ready");
  }

private:
  // Service server for receiving waypoints
  rclcpp::Service<kaskazi_drone::srv::GetWaypoints>::SharedPtr waypoint_service_;
  
  // MAVROS waypoint push client
  rclcpp::Client<mavros_msgs::srv::WaypointPush>::SharedPtr mavros_waypoint_client_;
  
  // Internal waypoint storage
  std::vector<mavros_msgs::msg::Waypoint> waypoint_list_;

  /**
   * @brief Service callback to receive waypoints from Motion Coordinator
   * @param request Shared pointer to service request containing waypoints
   * @param response Shared pointer to service response
   */
  void get_waypoints_callback(
    const std::shared_ptr<kaskazi_drone::srv::GetWaypoints::Request> request,
    std::shared_ptr<kaskazi_drone::srv::GetWaypoints::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "Received waypoint request with %zu waypoints", 
                request->waypoints.size());

    // Clear previous waypoints
    waypoint_list_.clear();

    // Convert geometry_msgs::Point waypoints to MAVROS waypoints
    for (size_t i = 0; i < request->waypoints.size(); ++i) {
      const auto& point = request->waypoints[i];
      
      // Create MAVROS waypoint message
      mavros_msgs::msg::Waypoint mavros_waypoint;
      
      // Configure waypoint parameters based on MAVROS Waypoint message structure
      mavros_waypoint.frame = 3;           // Global frame (GPS coordinates)
      mavros_waypoint.command = 16;        // MAV_CMD_NAV_WAYPOINT (fly to waypoint)
      mavros_waypoint.is_current = (i == 0) ? true : false;  // First waypoint is current
      mavros_waypoint.autocontinue = true; // Automatically continue to next waypoint
      
      // Set flight parameters
      mavros_waypoint.param1 = 0.0;        // Hold time in seconds (0 = don't hold)
      mavros_waypoint.param2 = 0.0;        // Acceptance radius in meters (0 = use default)
      mavros_waypoint.param3 = 0.0;        // Pass radius (0 = straight line)
      mavros_waypoint.param4 = 0.0;        // Yaw angle in degrees (0 = face next waypoint)
      
      // Set GPS coordinates (assuming input points are in local coordinates)
      // For this implementation, we'll treat the input points as GPS coordinates
      mavros_waypoint.x_lat = point.x;     // Latitude
      mavros_waypoint.y_long = point.y;    // Longitude  
      mavros_waypoint.z_alt = point.z;     // Altitude
      
      waypoint_list_.push_back(mavros_waypoint);
      
      RCLCPP_INFO(this->get_logger(),
                  "Converted waypoint %zu: lat=%.7f, lon=%.7f, alt=%.2f",
                  i, mavros_waypoint.x_lat, mavros_waypoint.y_long, mavros_waypoint.z_alt);
    }

    // Send waypoints to MAVROS
    bool success = send_waypoints_to_mavros();
    
    // Prepare response
    response->success = success;
    if (success) {
      response->message = "Successfully sent " + std::to_string(waypoint_list_.size()) + 
                         " waypoints to MAVROS";
    } else {
      response->message = "Failed to send waypoints to MAVROS";
    }

    RCLCPP_INFO(this->get_logger(), "Waypoint service response: %s", response->message.c_str());
  }

  /**
   * @brief Send waypoints to MAVROS waypoint push service
   * @return True if waypoints were successfully sent, false otherwise
   */
  bool send_waypoints_to_mavros()
  {
    if (waypoint_list_.empty()) {
      RCLCPP_WARN(this->get_logger(), "No waypoints to send to MAVROS");
      return false;
    }

    // Create MAVROS waypoint push request
    auto request = std::make_shared<mavros_msgs::srv::WaypointPush::Request>();
    request->start_index = 0;  // Start from the beginning of the waypoint list
    
    // Copy our waypoints to the MAVROS request
    for (const auto& waypoint : waypoint_list_) {
      request->waypoints.push_back(waypoint);
    }

    RCLCPP_INFO(this->get_logger(), "Sending %zu waypoints to MAVROS", waypoint_list_.size());

    // Send the request to MAVROS
    auto future = mavros_waypoint_client_->async_send_request(request);
    
    // Wait for the response using future.wait_for() to avoid executor conflict
    auto status = future.wait_for(std::chrono::seconds(5));
    
    if (status == std::future_status::ready) {
      auto response = future.get();
      
      if (response->success) {
        RCLCPP_INFO(this->get_logger(), 
                    "Successfully sent waypoints to MAVROS: %s", 
                    response->success ? "true" : "false");
        return true;
      } else {
        RCLCPP_ERROR(this->get_logger(), "MAVROS rejected waypoints");
        return false;
      }
    } else {
      RCLCPP_ERROR(this->get_logger(), "Failed to send waypoints to MAVROS - timeout or failure");
      return false;
    }
  }
};

/**
 * @brief Main function - entry point for the drone control node
 * @param argc Number of command line arguments
 * @param argv Array of command line arguments
 * @return Exit status
 */
int main(int argc, char ** argv)
{
  // Initialize ROS 2
  rclcpp::init(argc, argv);

  // Create the drone control node
  auto drone_control_node = std::make_shared<DroneControlNode>(rclcpp::NodeOptions());

  // Spin the node to handle incoming requests
  try {
    rclcpp::spin(drone_control_node);
  } catch (const std::exception& e) {
    RCLCPP_ERROR(drone_control_node->get_logger(), "Exception in spin: %s", e.what());
  }

  // Clean shutdown
  drone_control_node.reset();
  rclcpp::shutdown();
  return 0;
}