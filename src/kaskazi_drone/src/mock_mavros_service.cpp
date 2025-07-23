/**
 * @file mock_mavros_service.cpp
 * @brief Mock MAVROS Service for Testing
 * 
 * This node provides a mock MAVROS waypoint push service for testing
 * the drone control integration without requiring full MAVROS setup.
 * 
 * This is a temporary solution until MAVROS is properly installed.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "kaskazi_drone/srv/get_waypoints.hpp"

// Mock MAVROS message types (simplified versions)
struct MockWaypoint {
  uint8_t frame;
  uint16_t command;
  bool is_current;
  bool autocontinue;
  float param1, param2, param3, param4;
  double x_lat, y_long, z_alt;
};

struct MockWaypointPushRequest {
  uint16_t start_index;
  std::vector<MockWaypoint> waypoints;
};

struct MockWaypointPushResponse {
  bool success;
  uint32_t wp_transfered;
};

class MockMavrosService : public rclcpp::Node
{
public:
  /**
   * @brief Constructor - initializes the mock MAVROS service
   */
  explicit MockMavrosService(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : Node("mock_mavros_service", options)
  {
    using namespace std::placeholders;

    // Create mock MAVROS waypoint push service
    mavros_service_ = this->create_service<kaskazi_drone::srv::GetWaypoints>(
      "/mavros/mission/push",
      std::bind(&MockMavrosService::mavros_waypoint_callback, this, _1, _2));

    RCLCPP_INFO(this->get_logger(), "Mock MAVROS waypoint service started at /mavros/mission/push");
  }

private:
  rclcpp::Service<kaskazi_drone::srv::GetWaypoints>::SharedPtr mavros_service_;

  /**
   * @brief Mock MAVROS waypoint service callback
   * @param request Service request with waypoints
   * @param response Service response
   */
  void mavros_waypoint_callback(
    const std::shared_ptr<kaskazi_drone::srv::GetWaypoints::Request> request,
    std::shared_ptr<kaskazi_drone::srv::GetWaypoints::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), 
                "Mock MAVROS received %zu waypoints for drone mission", 
                request->waypoints.size());

    // Log received waypoints
    for (size_t i = 0; i < request->waypoints.size(); ++i) {
      const auto& wp = request->waypoints[i];
      RCLCPP_INFO(this->get_logger(),
                  "Mock MAVROS Waypoint %zu: lat=%.7f, lon=%.7f, alt=%.2f",
                  i, wp.x, wp.y, wp.z);
    }

    // Simulate successful waypoint upload
    response->success = true;
    response->message = "Mock MAVROS: Successfully uploaded " + 
                       std::to_string(request->waypoints.size()) + " waypoints to drone";

    RCLCPP_INFO(this->get_logger(), 
                "Mock MAVROS: Waypoint upload completed successfully");
  }
};

/**
 * @brief Main function
 */
int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  auto mock_mavros = std::make_shared<MockMavrosService>();

  RCLCPP_INFO(mock_mavros->get_logger(), 
              "Mock MAVROS Service running - simulating waypoint uploads");

  try {
    rclcpp::spin(mock_mavros);
  } catch (const std::exception& e) {
    RCLCPP_ERROR(mock_mavros->get_logger(), "Exception: %s", e.what());
  }

  mock_mavros.reset();
  rclcpp::shutdown();
  return 0;
}