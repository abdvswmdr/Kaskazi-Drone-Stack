/**
 * @file movement_action_client.hpp
 * @brief Header file for Movement Action Client Node
 * 
 * This header defines the MovementActionClient class that sends movement goals
 * to the Motion Coordinator and handles the response, feedback, and results.
 */

#ifndef KASKAZI_DRONE__MOVEMENT_ACTION_CLIENT_HPP_
#define KASKAZI_DRONE__MOVEMENT_ACTION_CLIENT_HPP_

#include <functional>
#include <future>
#include <memory>
#include <string>

#include "kaskazi_drone/action/movement.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

/**
 * @brief Movement Action Client class for sending drone movement requests
 * 
 * This class implements an action client that communicates with the Motion Coordinator
 * to request drone movements. It handles all aspects of the action communication:
 * goal submission, feedback processing, and result handling.
 */
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
  explicit MovementActionClient(const rclcpp::NodeOptions & options);

  /**
   * @brief Sends a movement request to the action server
   * @param x Target X coordinate in meters
   * @param y Target Y coordinate in meters  
   * @param z Target Z coordinate in meters
   */
  void send_movement_request(double x, double y, double z);

private:
  // Action client pointer
  rclcpp_action::Client<Movement>::SharedPtr client_ptr_;

  /**
   * @brief Callback when server responds to goal request
   * @param goal_handle Shared pointer to goal handle (nullptr if rejected)
   */
  void goal_response_callback(const GoalHandleMovement::SharedPtr & goal_handle);

  /**
   * @brief Callback for continuous feedback during action execution
   * @param goal_handle Shared pointer to goal handle
   * @param feedback Pointer to feedback message containing current progress
   */
  void feedback_callback(
    GoalHandleMovement::SharedPtr,
    const std::shared_ptr<const Movement::Feedback> feedback);

  /**
   * @brief Callback when action execution is completed
   * @param result Wrapped result containing success status and message
   */
  void result_callback(const GoalHandleMovement::WrappedResult & result);
};

#endif  // KASKAZI_DRONE__MOVEMENT_ACTION_CLIENT_HPP_