/**
 * @file drone_control_node_simple.cpp
 * @brief Simplified Drone Control Node without MAVROS dependency
 * 
 * This is a simplified version that can work without MAVROS for initial testing.
 * It demonstrates the complete integration flow and can be upgraded to full
 * MAVROS integration once MAVROS is properly installed.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "kaskazi_drone/srv/get_waypoints.hpp"
#include "geometry_msgs/msg/point.hpp"

class DroneControlNodeSimple : public rclcpp::Node
{
public:
  /**
   * @brief Constructor - initializes the simplified drone control node
   */
  explicit DroneControlNodeSimple(const rclcpp::NodeOptions & options)
  : Node("drone_control_node_simple", options)
  {
    using namespace std::placeholders;

    // Create service server to receive waypoints from Motion Coordinator
    waypoint_service_ = this->create_service<kaskazi_drone::srv::GetWaypoints>(
      "waypoint_push_topic",
      std::bind(&DroneControlNodeSimple::get_waypoints_callback, this, _1, _2));

    // Create client for mock MAVROS service (or real MAVROS when available)
    mock_mavros_client_ = this->create_client<kaskazi_drone::srv::GetWaypoints>(
      "/mavros/mission/push");

    // Wait for mock MAVROS service
    while (!mock_mavros_client_->wait_for_service(std::chrono::seconds(1))) {
      if (!rclcpp::ok()) {
        RCLCPP_ERROR(this->get_logger(), "Interrupted while waiting for mock MAVROS service");
        return;
      }
      RCLCPP_INFO(this->get_logger(), "Mock MAVROS service not available, looking for /mavros/mission/push");
    }

    RCLCPP_INFO(this->get_logger(), "Simplified Drone Control Node initialized and ready");
  }

private:
  rclcpp::Service<kaskazi_drone::srv::GetWaypoints>::SharedPtr waypoint_service_;
  rclcpp::Client<kaskazi_drone::srv::GetWaypoints>::SharedPtr mock_mavros_client_;

  /**
   * @brief Service callback to receive waypoints from Motion Coordinator
   */
  void get_waypoints_callback(
    const std::shared_ptr<kaskazi_drone::srv::GetWaypoints::Request> request,
    std::shared_ptr<kaskazi_drone::srv::GetWaypoints::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), 
                "Received waypoint request with %zu waypoints", 
                request->waypoints.size());

    // Log received waypoints
    for (size_t i = 0; i < request->waypoints.size(); ++i) {
      const auto& point = request->waypoints[i];
      RCLCPP_INFO(this->get_logger(),
                  "Waypoint %zu: lat=%.7f, lon=%.7f, alt=%.2f",
                  i, point.x, point.y, point.z);
    }

    // Forward waypoints to mock MAVROS service
    bool success = send_to_mock_mavros(request->waypoints);
    
    response->success = success;
    if (success) {
      response->message = "Successfully processed " + std::to_string(request->waypoints.size()) + 
                         " waypoints and sent to drone via mock MAVROS";
    } else {
      response->message = "Failed to send waypoints to mock MAVROS";
    }

    RCLCPP_INFO(this->get_logger(), "Response: %s", response->message.c_str());
  }

  /**
   * @brief Send waypoints to mock MAVROS service
   */
  bool send_to_mock_mavros(const std::vector<geometry_msgs::msg::Point>& waypoints)
  {
    auto request = std::make_shared<kaskazi_drone::srv::GetWaypoints::Request>();
    request->waypoints = waypoints;

    RCLCPP_INFO(this->get_logger(), "Forwarding %zu waypoints to mock MAVROS", waypoints.size());

    auto future = mock_mavros_client_->async_send_request(request);
    
    if (rclcpp::spin_until_future_complete(this->get_node_base_interface(), future, 
                                          std::chrono::seconds(5)) == 
        rclcpp::FutureReturnCode::SUCCESS) {
      
      auto response = future.get();
      
      if (response->success) {
        RCLCPP_INFO(this->get_logger(), 
                    "Mock MAVROS accepted waypoints: %s", 
                    response->message.c_str());
        return true;
      } else {
        RCLCPP_ERROR(this->get_logger(), 
                    "Mock MAVROS rejected waypoints: %s", 
                    response->message.c_str());
        return false;
      }
    } else {
      RCLCPP_ERROR(this->get_logger(), "Failed to communicate with mock MAVROS - timeout");
      return false;
    }
  }
};

/**
 * @brief Main function
 */
int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  auto drone_control = std::make_shared<DroneControlNodeSimple>(rclcpp::NodeOptions());

  try {
    rclcpp::spin(drone_control);
  } catch (const std::exception& e) {
    RCLCPP_ERROR(drone_control->get_logger(), "Exception: %s", e.what());
  }

  drone_control.reset();
  rclcpp::shutdown();
  return 0;
}