cmake_minimum_required(VERSION 3.8)
project(kaskazi_drone)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(action_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)
# MAVROS is now installed - require it
find_package(mavros_msgs REQUIRED)

# Generate action and service interfaces
rosidl_generate_interfaces(${PROJECT_NAME}
  "action/Movement.action"
  "srv/GetWaypoints.srv"
  DEPENDENCIES builtin_interfaces geometry_msgs
)

# Include directories
include_directories(include)

# Create executables
add_executable(movement_action_client src/movement_action_client.cpp)
ament_target_dependencies(movement_action_client 
  rclcpp 
  rclcpp_action 
  rclcpp_components
)

add_executable(motion_coordinator 
  src/motion_coordinator.cpp
  src/astar_planner.cpp
)
ament_target_dependencies(motion_coordinator 
  rclcpp 
  rclcpp_action 
  rclcpp_components
  geometry_msgs
  nav_msgs
)

# Full MAVROS drone control node (MAVROS is now installed)
message(STATUS "Building full MAVROS drone control node")
add_executable(drone_control_node src/drone_control_node.cpp)
ament_target_dependencies(drone_control_node 
  rclcpp 
  rclcpp_components
  geometry_msgs
  mavros_msgs
)

# Simplified drone control node (without MAVROS dependency)
add_executable(drone_control_node_simple src/drone_control_node_simple.cpp)
ament_target_dependencies(drone_control_node_simple 
  rclcpp 
  rclcpp_components
  geometry_msgs
)

# Mock MAVROS service for testing
add_executable(mock_mavros_service src/mock_mavros_service.cpp)
ament_target_dependencies(mock_mavros_service 
  rclcpp 
  rclcpp_components
  geometry_msgs
)

# Link action and service interfaces to executables
rosidl_target_interfaces(movement_action_client ${PROJECT_NAME} "rosidl_typesupport_cpp")
rosidl_target_interfaces(motion_coordinator ${PROJECT_NAME} "rosidl_typesupport_cpp")
rosidl_target_interfaces(drone_control_node ${PROJECT_NAME} "rosidl_typesupport_cpp")
rosidl_target_interfaces(drone_control_node_simple ${PROJECT_NAME} "rosidl_typesupport_cpp")
rosidl_target_interfaces(mock_mavros_service ${PROJECT_NAME} "rosidl_typesupport_cpp")

# Install executables
install(TARGETS
  movement_action_client
  motion_coordinator
  drone_control_node
  drone_control_node_simple
  mock_mavros_service
  DESTINATION lib/${PROJECT_NAME}
)

# Install directories
install(DIRECTORY
  launch
  config
  worlds
  urdf
  DESTINATION share/${PROJECT_NAME}/
)


if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

# Export dependencies for downstream packages
ament_export_dependencies(rosidl_default_runtime)

ament_package()