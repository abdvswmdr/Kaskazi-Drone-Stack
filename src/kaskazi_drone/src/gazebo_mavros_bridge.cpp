/**
 * @file gazebo_mavros_bridge.cpp
 * @brief Bridge between MAVROS waypoints and Gazebo multicopter control
 * 
 * This node provides the integration between MAVROS and Gazebo simulation.
 * It receives waypoint commands from MAVROS and converts them to velocity
 * commands for the Gazebo multicopter control plugin.
 * 
 * Topics:
 * - Subscribes: /mavros/setpoint_position/local (geometry_msgs/PoseStamped)
 * - Publishes: /crazyflie/cmd_vel (geometry_msgs/Twist)
 * - Publishes: /crazyflie/enable (std_msgs/Bool)
 */

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <std_msgs/msg/bool.hpp>
#include <mavros_msgs/msg/state.hpp>
// #include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>  // Not needed for basic operation
#include <cmath>

class GazeboMavrosbridge : public rclcpp::Node
{
public:
    /**
     * @brief Constructor - initializes publishers and subscribers
     */
    explicit GazeboMavrosbridge(const rclcpp::NodeOptions & options)
    : Node("gazebo_mavros_bridge", options)
    {
        // Publishers to Gazebo
        cmd_vel_pub_ = this->create_publisher<geometry_msgs::msg::Twist>(
            "/crazyflie/cmd_vel", 10);
        enable_pub_ = this->create_publisher<std_msgs::msg::Bool>(
            "/crazyflie/enable", 10);

        // Subscribers from MAVROS and Gazebo
        setpoint_sub_ = this->create_subscription<geometry_msgs::msg::PoseStamped>(
            "/mavros/setpoint_position/local", 10,
            std::bind(&GazeboMavrosbridge::setpoint_callback, this, std::placeholders::_1));
        
        odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
            "/odom", 10,
            std::bind(&GazeboMavrosbridge::odom_callback, this, std::placeholders::_1));

        mavros_state_sub_ = this->create_subscription<mavros_msgs::msg::State>(
            "/mavros/state", 10,
            std::bind(&GazeboMavrosbridge::state_callback, this, std::placeholders::_1));

        // Control timer for continuous control loop
        control_timer_ = this->create_wall_timer(
            std::chrono::milliseconds(50),  // 20 Hz control loop
            std::bind(&GazeboMavrosbridge::control_loop, this));

        // Initialize variables
        current_pose_.position.x = 0.0;
        current_pose_.position.y = 0.0;
        current_pose_.position.z = 0.0;
        
        target_pose_.position.x = 0.0;
        target_pose_.position.y = 0.0;
        target_pose_.position.z = 1.0;  // Start with 1m altitude
        
        armed_ = false;
        mode_ = "MANUAL";

        // PID gains for position control
        kp_xy_ = 2.0;
        kp_z_ = 2.5;
        max_vel_xy_ = 2.0;  // m/s
        max_vel_z_ = 1.0;   // m/s
        position_tolerance_ = 0.1;  // meters

        RCLCPP_INFO(this->get_logger(), "Gazebo-MAVROS Bridge initialized");
        
        // Enable the gazebo controller by default
        std_msgs::msg::Bool enable_msg;
        enable_msg.data = true;
        enable_pub_->publish(enable_msg);
    }

private:
    // ROS publishers and subscribers
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr enable_pub_;
    rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr setpoint_sub_;
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
    rclcpp::Subscription<mavros_msgs::msg::State>::SharedPtr mavros_state_sub_;
    rclcpp::TimerBase::SharedPtr control_timer_;

    // State variables
    geometry_msgs::msg::Pose current_pose_;
    geometry_msgs::msg::Pose target_pose_;
    bool armed_;
    std::string mode_;

    // Control parameters
    double kp_xy_, kp_z_;
    double max_vel_xy_, max_vel_z_;
    double position_tolerance_;

    /**
     * @brief Callback for MAVROS setpoint commands
     * @param msg Pose setpoint from MAVROS
     */
    void setpoint_callback(const geometry_msgs::msg::PoseStamped::SharedPtr msg)
    {
        target_pose_ = msg->pose;
        
        RCLCPP_DEBUG(this->get_logger(), 
                    "Received setpoint: x=%.2f, y=%.2f, z=%.2f",
                    target_pose_.position.x, 
                    target_pose_.position.y, 
                    target_pose_.position.z);
    }

    /**
     * @brief Callback for odometry data from Gazebo
     * @param msg Odometry message from Gazebo
     */
    void odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
    {
        current_pose_ = msg->pose.pose;
    }

    /**
     * @brief Callback for MAVROS state information
     * @param msg MAVROS state message
     */
    void state_callback(const mavros_msgs::msg::State::SharedPtr msg)
    {
        armed_ = msg->armed;
        mode_ = msg->mode;
        
        // Enable/disable gazebo controller based on MAVROS arm state
        std_msgs::msg::Bool enable_msg;
        enable_msg.data = armed_ && (mode_ == "OFFBOARD" || mode_ == "AUTO");
        enable_pub_->publish(enable_msg);
    }

    /**
     * @brief Main control loop - converts position setpoints to velocity commands
     */
    void control_loop()
    {
        if (!armed_) {
            // Send zero velocity if not armed
            geometry_msgs::msg::Twist cmd_vel;
            cmd_vel_pub_->publish(cmd_vel);
            return;
        }

        // Calculate position errors
        double error_x = target_pose_.position.x - current_pose_.position.x;
        double error_y = target_pose_.position.y - current_pose_.position.y;
        double error_z = target_pose_.position.z - current_pose_.position.z;

        // Calculate velocity commands using proportional control
        geometry_msgs::msg::Twist cmd_vel;
        
        cmd_vel.linear.x = kp_xy_ * error_x;
        cmd_vel.linear.y = kp_xy_ * error_y;
        cmd_vel.linear.z = kp_z_ * error_z;

        // Apply velocity limits
        cmd_vel.linear.x = std::clamp(cmd_vel.linear.x, -max_vel_xy_, max_vel_xy_);
        cmd_vel.linear.y = std::clamp(cmd_vel.linear.y, -max_vel_xy_, max_vel_xy_);
        cmd_vel.linear.z = std::clamp(cmd_vel.linear.z, -max_vel_z_, max_vel_z_);

        // Angular velocity control (simplified - maintain level flight)
        cmd_vel.angular.x = 0.0;
        cmd_vel.angular.y = 0.0;
        cmd_vel.angular.z = 0.0;

        // Publish velocity command
        cmd_vel_pub_->publish(cmd_vel);

        // Log debug information
        double distance = std::sqrt(error_x*error_x + error_y*error_y + error_z*error_z);
        
        RCLCPP_DEBUG(this->get_logger(),
                    "Control: pos(%.2f,%.2f,%.2f) -> target(%.2f,%.2f,%.2f), "
                    "error(%.2f,%.2f,%.2f), vel(%.2f,%.2f,%.2f), dist=%.2f",
                    current_pose_.position.x, current_pose_.position.y, current_pose_.position.z,
                    target_pose_.position.x, target_pose_.position.y, target_pose_.position.z,
                    error_x, error_y, error_z,
                    cmd_vel.linear.x, cmd_vel.linear.y, cmd_vel.linear.z,
                    distance);
    }
};

/**
 * @brief Main function - entry point for the gazebo-mavros bridge
 * @param argc Number of command line arguments
 * @param argv Array of command line arguments
 * @return Exit status
 */
int main(int argc, char ** argv)
{
    // Initialize ROS 2
    rclcpp::init(argc, argv);

    // Create the bridge node
    auto bridge_node = std::make_shared<GazeboMavrosbridge>(rclcpp::NodeOptions());

    // Spin the node
    try {
        rclcpp::spin(bridge_node);
    } catch (const std::exception& e) {
        RCLCPP_ERROR(bridge_node->get_logger(), "Exception in spin: %s", e.what());
    }

    // Clean shutdown
    bridge_node.reset();
    rclcpp::shutdown();
    return 0;
}