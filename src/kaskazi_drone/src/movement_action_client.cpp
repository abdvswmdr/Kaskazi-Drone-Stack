/**
 * @file movement_action_client.cpp
 * @brief Movement Action Client Node for Drone Navigation
 * 
 * This node acts as an action client that sends movement goals (x, y, z coordinates)
 * to the Motion Coordinator server and receives feedback about the movement progress.
 * 
 * The client handles:
 * - Sending movement requests with target coordinates
 * - Receiving goal acceptance/rejection responses
 * - Processing continuous feedback during movement execution
 * - Handling final results (success/failure)
 */

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <sstream>

#include "kaskazi_drone/action/movement.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "rclcpp_components/register_node_macro.hpp"

class MovementActionClient : public rclcpp::Node
{
public:
  // Type aliases for cleaner code
  using Movement = kaskazi_drone::action::Movement;
  using GoalHandleMovement = rclcpp_action::ClientGoalHandle<Movement>;

  /**
   * @brief Constructor - initializes the action client
   * @param options Node options for configuration
   */
  explicit MovementActionClient(const rclcpp::NodeOptions & options)
  : Node("movement_action_client", options)
  {
    // Create action client for movement actions
    this->client_ptr_ = rclcpp_action::create_client<Movement>(
      this,
      "movement_action_topic");  // Action topic name

    // Declare parameters for target coordinates
    this->declare_parameter<double>("x_param", 0.0);
    this->declare_parameter<double>("y_param", 0.0);
    this->declare_parameter<double>("z_param", 0.0);
  }

  /**
   * @brief Sends a movement request to the action server
   * @param x Target X coordinate in meters
   * @param y Target Y coordinate in meters
   * @param z Target Z coordinate in meters
   */
  void send_movement_request(double x, double y, double z)
  {
    using namespace std::placeholders;

    // Check if action server is available
    if (!this->client_ptr_->wait_for_action_server(std::chrono::seconds(2))) {
      RCLCPP_ERROR(this->get_logger(), "Action server not available");
      return;
    }

    // Create goal message with target coordinates
    auto goal_msg = Movement::Goal();
    goal_msg.x = x;
    goal_msg.y = y;
    goal_msg.z = z;

    RCLCPP_INFO(this->get_logger(), "Sending movement goal: x=%.2f, y=%.2f, z=%.2f", x, y, z);

    // Configure send options with callbacks
    auto send_goal_options = rclcpp_action::Client<Movement>::SendGoalOptions();
    
    // Callback when server responds to goal request
    send_goal_options.goal_response_callback =
      std::bind(&MovementActionClient::goal_response_callback, this, _1);
    
    // Callback for continuous feedback during execution
    send_goal_options.feedback_callback =
      std::bind(&MovementActionClient::feedback_callback, this, _1, _2);
    
    // Callback when action is completed
    send_goal_options.result_callback =
      std::bind(&MovementActionClient::result_callback, this, _1);

    // Send the goal to the action server
    this->client_ptr_->async_send_goal(goal_msg, send_goal_options);
  }

private:
  // Action client pointer
  rclcpp_action::Client<Movement>::SharedPtr client_ptr_;

  /**
   * @brief Callback when server responds to goal request
   * @param goal_handle Shared pointer to goal handle (nullptr if rejected)
   */
  void goal_response_callback(const GoalHandleMovement::SharedPtr & goal_handle)
  {
    if (!goal_handle) {
      RCLCPP_ERROR(this->get_logger(), "Goal was rejected by server");
      return;
    }

    RCLCPP_INFO(this->get_logger(), "Goal accepted by server, waiting for result");
  }

  /**
   * @brief Callback for continuous feedback during action execution
   * @param goal_handle Shared pointer to goal handle
   * @param feedback Pointer to feedback message containing current progress
   */
  void feedback_callback(
    GoalHandleMovement::SharedPtr,
    const std::shared_ptr<const Movement::Feedback> feedback)
  {
    // Log current position and progress
    RCLCPP_INFO(
      this->get_logger(),
      "Feedback - Current position: x=%.2f, y=%.2f, z=%.2f, Progress: %.1f%%",
      feedback->current_x,
      feedback->current_y,
      feedback->current_z,
      feedback->progress * 100.0
    );
  }

  /**
   * @brief Callback when action execution is completed
   * @param result Wrapped result containing success status and message
   */
  void result_callback(const GoalHandleMovement::WrappedResult & result)
  {
    switch (result.code) {
      case rclcpp_action::ResultCode::SUCCEEDED:
        RCLCPP_INFO(this->get_logger(), "Action succeeded: %s", 
                   result.result->message.c_str());
        break;
      case rclcpp_action::ResultCode::ABORTED:
        RCLCPP_ERROR(this->get_logger(), "Action was aborted: %s", 
                    result.result->message.c_str());
        break;
      case rclcpp_action::ResultCode::CANCELED:
        RCLCPP_ERROR(this->get_logger(), "Action was canceled");
        break;
      default:
        RCLCPP_ERROR(this->get_logger(), "Unknown result code");
        break;
    }
    
    // Shutdown the node after receiving result
    rclcpp::shutdown();
  }
};

/**
 * @brief Main function - entry point for the movement action client
 * @param argc Number of command line arguments
 * @param argv Array of command line arguments
 * @return Exit status
 */
int main(int argc, char ** argv)
{
  // Initialize ROS 2
  rclcpp::init(argc, argv);

  // Create the movement action client node
  auto movement_client = std::make_shared<MovementActionClient>(rclcpp::NodeOptions());

  // Get target coordinates from parameters
  double x_param = movement_client->get_parameter("x_param").get_parameter_value().get<double>();
  double y_param = movement_client->get_parameter("y_param").get_parameter_value().get<double>();
  double z_param = movement_client->get_parameter("z_param").get_parameter_value().get<double>();

  // Send movement request with the specified coordinates
  movement_client->send_movement_request(x_param, y_param, z_param);

  // Spin the node to handle callbacks
  try {
    rclcpp::spin(movement_client);
  } catch (const std::exception& e) {
    RCLCPP_ERROR(movement_client->get_logger(), "Exception in spin: %s", e.what());
  }

  // Clean shutdown
  movement_client.reset();
  rclcpp::shutdown();
  return 0;
}