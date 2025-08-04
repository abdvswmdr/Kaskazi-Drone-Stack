/**
 * @file motion_coordinator.cpp
 * @brief Motion Coordinator Node with A* Path Planning
 * 
 * This node acts as an action server that receives movement goals from the client,
 * performs A* path planning with obstacle avoidance, and provides continuous feedback
 * about the planning and waypoint generation process.
 * 
 * The coordinator handles:
 * - Receiving movement requests with target coordinates
 * - Converting GPS coordinates to grid coordinates
 * - Performing A* pathfinding with obstacle avoidance
 * - Converting planned path back to GPS waypoints
 * - Providing feedback during the planning process
 */

#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include <cmath>

#include "kaskazi_drone/action/movement.hpp"
#include "kaskazi_drone/astar_planner.hpp"
#include "kaskazi_drone/srv/get_waypoints.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "geometry_msgs/msg/point.hpp"

class MotionCoordinator : public rclcpp::Node
{
public:
  // Type aliases for cleaner code
  using Movement = kaskazi_drone::action::Movement;
  using GoalHandleMovement = rclcpp_action::ServerGoalHandle<Movement>;

  /**
   * @brief Constructor - initializes the action server and A* planner
   * @param options Node options for configuration
   */
  explicit MotionCoordinator(const rclcpp::NodeOptions & options)
  : Node("motion_coordinator", options)
  {
    using namespace std::placeholders;

    // Create action server for movement actions
    this->action_server_ = rclcpp_action::create_server<Movement>(
      this,
      "movement_action_topic",  // Action topic name
      std::bind(&MotionCoordinator::handle_goal, this, _1, _2),
      std::bind(&MotionCoordinator::handle_cancel, this, _1),
      std::bind(&MotionCoordinator::handle_accepted, this, _1));

    // Create client for waypoint service to communicate with Drone Control Node
    waypoint_client_ = this->create_client<kaskazi_drone::srv::GetWaypoints>(
      "waypoint_push_topic");

    // Wait for waypoint service to be available
    while (!waypoint_client_->wait_for_service(std::chrono::seconds(1))) {
      if (!rclcpp::ok()) {
        RCLCPP_ERROR(this->get_logger(), "Interrupted while waiting for waypoint service");
        return;
      }
      RCLCPP_INFO(this->get_logger(), "Waypoint service not available, looking for waypoint_push_topic");
    }

    // Initialize GPS coordinates and grid setup
    setup_environment();

    RCLCPP_INFO(this->get_logger(), "Motion Coordinator started and ready to receive goals");
  }

private:
  // Action server pointer
  rclcpp_action::Server<Movement>::SharedPtr action_server_;
  
  // Waypoint service client
  rclcpp::Client<kaskazi_drone::srv::GetWaypoints>::SharedPtr waypoint_client_;
  
  // A* planner and grid
  std::shared_ptr<AStar> astar_planner_;
  std::shared_ptr<Grid3D> grid_;
  
  // Environment setup parameters
  GPSCoordinate home_gps_;      // Home GPS position (47.3977508, 8.5456074, 535.35)
  GridCoordinate home_grid_;    // Home position in grid coordinates
  std::vector<GridCoordinate> obstacles_;  // List of obstacles in grid coordinates
  
  // Grid dimensions (increased to handle larger goals)
  int grid_rows_ = 15;
  int grid_cols_ = 15;
  int grid_height_ = 15;

  /**
   * @brief Setup the environment with obstacles and grid configuration
   */
  void setup_environment()
  {
    // Set home GPS position (ETH Zurich coordinates from Python code)
    home_gps_ = GPSCoordinate(47.3977508, 8.5456074, 535.35);
    home_grid_ = GridCoordinate(7, 7, 7);  // Center of 15x15x15 grid
    
    // Setup example obstacles (converted from GPS to grid coordinates)
    std::vector<GPSCoordinate> gps_obstacles = {
      GPSCoordinate(47.3977510, 8.5456080, 536.0),
      GPSCoordinate(47.3977520, 8.5456090, 537.0)
    };
    
    // Create temporary AStar instance for coordinate conversion
    auto temp_grid = std::make_shared<Grid3D>(grid_rows_, grid_cols_, grid_height_, std::vector<GridCoordinate>());
    auto temp_astar = std::make_shared<AStar>(temp_grid);
    
    // Convert GPS obstacles to grid coordinates
    for (const auto& gps_obs : gps_obstacles) {
      GridCoordinate grid_obs = temp_astar->gps_to_grid(gps_obs, home_gps_, home_grid_);
      obstacles_.push_back(grid_obs);
    }
    
    // Create actual grid with obstacles
    grid_ = std::make_shared<Grid3D>(grid_rows_, grid_cols_, grid_height_, obstacles_);
    astar_planner_ = std::make_shared<AStar>(grid_);
    
    RCLCPP_INFO(this->get_logger(), "Environment setup complete with %zu obstacles", obstacles_.size());
  }

  /**
   * @brief Handle incoming goal requests
   * @param uuid Goal UUID
   * @param goal Pointer to goal message
   * @return Goal response (ACCEPT_AND_EXECUTE or REJECT)
   */
  rclcpp_action::GoalResponse handle_goal(
    const rclcpp_action::GoalUUID & uuid,
    std::shared_ptr<const Movement::Goal> goal)
  {
    (void)uuid;  // Suppress unused parameter warning
    
    RCLCPP_INFO(this->get_logger(), 
                "Received movement goal: x=%.2f, y=%.2f, z=%.2f",
                goal->x, goal->y, goal->z);
    
    // Validate goal coordinates (basic bounds checking)
    if (std::abs(goal->x) > 100 || std::abs(goal->y) > 100 || goal->z < 0 || goal->z > 100) {
      RCLCPP_WARN(this->get_logger(), "Goal coordinates out of acceptable range");
      return rclcpp_action::GoalResponse::REJECT;
    }
    
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  /**
   * @brief Handle goal cancellation requests
   * @param goal_handle Shared pointer to goal handle
   * @return Cancellation response (ACCEPT or REJECT)
   */
  rclcpp_action::CancelResponse handle_cancel(
    const std::shared_ptr<GoalHandleMovement> goal_handle)
  {
    (void)goal_handle;  // Suppress unused parameter warning
    RCLCPP_INFO(this->get_logger(), "Received request to cancel goal");
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  /**
   * @brief Execute the accepted goal (perform path planning)
   * @param goal_handle Shared pointer to goal handle
   */
  void handle_accepted(const std::shared_ptr<GoalHandleMovement> goal_handle)
  {
    using namespace std::placeholders;
    
    // Execute path planning in a separate thread to avoid blocking
    std::thread execution_thread(
      std::bind(&MotionCoordinator::execute_planning, this, _1), 
      goal_handle);
    execution_thread.detach();
  }

  /**
   * @brief Execute path planning and provide feedback
   * @param goal_handle Shared pointer to goal handle
   */
  void execute_planning(const std::shared_ptr<GoalHandleMovement> goal_handle)
  {
    const auto goal = goal_handle->get_goal();
    auto feedback = std::make_shared<Movement::Feedback>();
    auto result = std::make_shared<Movement::Result>();
    
    RCLCPP_INFO(this->get_logger(), 
                "Starting path planning for goal: x=%.2f, y=%.2f, z=%.2f",
                goal->x, goal->y, goal->z);
    
    // Convert target coordinates to GPS and then to grid
    // More accurate local to GPS conversion for Zurich area (latitude ~47.4°)
    // 1 degree latitude ≈ 111,000 meters globally
    // 1 degree longitude ≈ 111,000 * cos(latitude) meters
    double lat_conversion = 1.0 / 111000.0;  // meters to degrees latitude
    double lon_conversion = 1.0 / (111000.0 * cos(home_gps_.latitude * M_PI / 180.0));  // meters to degrees longitude
    
    GPSCoordinate target_gps(
      home_gps_.latitude + (goal->x * lat_conversion),   // North (X) -> Latitude
      home_gps_.longitude + (goal->y * lon_conversion),  // East (Y) -> Longitude  
      home_gps_.altitude + goal->z                       // Up (Z) -> Altitude
    );
    
    GridCoordinate start_grid = home_grid_;  // Start from home position
    GridCoordinate goal_grid = astar_planner_->gps_to_grid(target_gps, home_gps_, home_grid_);
    
    RCLCPP_INFO(this->get_logger(), 
                "Grid coordinates - Start: (%d,%d,%d), Goal: (%d,%d,%d)",
                start_grid.x, start_grid.y, start_grid.z,
                goal_grid.x, goal_grid.y, goal_grid.z);
    
    // Provide initial feedback
    feedback->current_x = 0.0;
    feedback->current_y = 0.0;
    feedback->current_z = 0.0;
    feedback->progress = 0.0;
    goal_handle->publish_feedback(feedback);
    
    // Simulate planning progress with feedback
    for (int step = 1; step <= 5; ++step) {
      // Check if goal was cancelled
      if (goal_handle->is_canceling()) {
        result->success = false;
        result->message = "Goal was cancelled during planning";
        goal_handle->canceled(result);
        return;
      }
      
      // Update progress feedback
      feedback->progress = static_cast<double>(step) / 5.0;
      goal_handle->publish_feedback(feedback);
      
      RCLCPP_INFO(this->get_logger(), "Planning progress: %.0f%%", feedback->progress * 100.0);
      
      // Simulate processing time
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Perform actual A* path planning
    std::vector<GridCoordinate> path = astar_planner_->find_path(start_grid, goal_grid);
    
    if (path.empty()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to find a valid path to the goal");
      result->success = false;
      result->message = "A* failed to find a valid path";
      goal_handle->abort(result);
      return;
    }
    
    // Convert path back to GPS waypoints
    std::vector<GPSCoordinate> gps_waypoints;
    for (const auto& grid_coord : path) {
      GPSCoordinate gps_coord = astar_planner_->grid_to_gps(grid_coord, home_gps_, home_grid_);
      gps_waypoints.push_back(gps_coord);
    }
    
    RCLCPP_INFO(this->get_logger(), 
                "Path planning successful! Generated %zu waypoints",
                gps_waypoints.size());
    
    // Convert GPS waypoints to local coordinates (meters) for the drone control node
    std::vector<geometry_msgs::msg::Point> waypoint_points;
    for (const auto& gps_coord : gps_waypoints) {
      geometry_msgs::msg::Point point;
      
      // Convert GPS to local coordinates in meters (relative to home position)
      // Latitude difference -> North/South distance (X)
      // Longitude difference -> East/West distance (Y)
      double lat_diff = gps_coord.latitude - home_gps_.latitude;
      double lon_diff = gps_coord.longitude - home_gps_.longitude;
      
      // Convert degrees to meters
      // 1 degree latitude ≈ 111,000 meters
      // 1 degree longitude ≈ 111,000 * cos(latitude) meters
      point.x = lat_diff * 111000.0; // North (meters)
      point.y = lon_diff * 111000.0 * cos(home_gps_.latitude * M_PI / 180.0); // East (meters)
      point.z = gps_coord.altitude; // Keep altitude as is
      
      waypoint_points.push_back(point);
      
      RCLCPP_INFO(this->get_logger(),
                  "Waypoint %zu: local(%.2f, %.2f, %.2f) from GPS(%.7f, %.7f, %.2f)",
                  waypoint_points.size() - 1, point.x, point.y, point.z,
                  gps_coord.latitude, gps_coord.longitude, gps_coord.altitude);
    }

    // Send waypoints to Drone Control Node
    bool waypoint_success = send_waypoints_to_drone_control(waypoint_points);
    
    // Final feedback with target position
    feedback->current_x = goal->x;
    feedback->current_y = goal->y;
    feedback->current_z = goal->z;
    feedback->progress = 1.0;
    goal_handle->publish_feedback(feedback);
    
    // Set result based on both path planning and waypoint sending success
    result->success = waypoint_success;
    if (waypoint_success) {
      result->message = "Path planning completed successfully with " + 
                       std::to_string(gps_waypoints.size()) + " waypoints generated and sent to drone";
    } else {
      result->message = "Path planning successful but failed to send waypoints to drone control";
    }
    
    goal_handle->succeed(result);
    RCLCPP_INFO(this->get_logger(), "Goal completed: %s", result->message.c_str());
  }

  /**
   * @brief Send waypoints to Drone Control Node via service call
   * @param waypoints Vector of waypoint coordinates to send
   * @return True if waypoints were successfully sent, false otherwise
   */
  bool send_waypoints_to_drone_control(const std::vector<geometry_msgs::msg::Point>& waypoints)
  {
    // Create service request
    auto request = std::make_shared<kaskazi_drone::srv::GetWaypoints::Request>();
    request->waypoints = waypoints;

    RCLCPP_INFO(this->get_logger(), "Sending %zu waypoints to Drone Control Node", waypoints.size());

    // Send the request
    auto future = waypoint_client_->async_send_request(request);
    
    // Wait for the response using future.wait_for() to avoid executor conflict
    auto status = future.wait_for(std::chrono::seconds(10));
    
    if (status == std::future_status::ready) {
      auto response = future.get();
      
      if (response->success) {
        RCLCPP_INFO(this->get_logger(), 
                    "Successfully sent waypoints to Drone Control: %s", 
                    response->message.c_str());
        return true;
      } else {
        RCLCPP_ERROR(this->get_logger(), 
                    "Drone Control rejected waypoints: %s", 
                    response->message.c_str());
        return false;
      }
    } else {
      RCLCPP_ERROR(this->get_logger(), "Failed to send waypoints to Drone Control - timeout or failure");
      return false;
    }
  }
};

/**
 * @brief Main function - entry point for the motion coordinator
 * @param argc Number of command line arguments
 * @param argv Array of command line arguments
 * @return Exit status
 */
int main(int argc, char ** argv)
{
  // Initialize ROS 2
  rclcpp::init(argc, argv);

  // Create the motion coordinator node
  auto motion_coordinator = std::make_shared<MotionCoordinator>(rclcpp::NodeOptions());

  // Spin the node to handle incoming requests
  try {
    rclcpp::spin(motion_coordinator);
  } catch (const std::exception& e) {
    RCLCPP_ERROR(motion_coordinator->get_logger(), "Exception in spin: %s", e.what());
  }

  // Clean shutdown
  motion_coordinator.reset();
  rclcpp::shutdown();
  return 0;
}