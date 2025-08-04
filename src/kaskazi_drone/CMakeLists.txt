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
# PX4 native DDS communication
find_package(px4_msgs REQUIRED)

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

# PX4 native DDS drone control node
message(STATUS "Building PX4 DDS drone control node")
add_executable(drone_control_node src/drone_control_node.cpp)
ament_target_dependencies(drone_control_node 
  rclcpp 
  rclcpp_components
  geometry_msgs
  px4_msgs
)

# Note: drone_control_node_simple and mock_mavros_service not implemented yet
# Commented out until source files are created
# add_executable(drone_control_node_simple src/drone_control_node_simple.cpp)
# add_executable(mock_mavros_service src/mock_mavros_service.cpp)

# Gazebo-PX4 bridge (optional utility)
add_executable(gazebo_px4_bridge src/gazebo_px4_bridge.cpp)
ament_target_dependencies(gazebo_px4_bridge 
  rclcpp 
  rclcpp_components
  geometry_msgs
  nav_msgs
  std_msgs
  px4_msgs
)

# Link action and service interfaces to executables (ROS2 Jazzy modern syntax)
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")
target_link_libraries(movement_action_client "${cpp_typesupport_target}")
target_link_libraries(motion_coordinator "${cpp_typesupport_target}")
target_link_libraries(drone_control_node "${cpp_typesupport_target}")
target_link_libraries(gazebo_px4_bridge "${cpp_typesupport_target}")
# Note: drone_control_node_simple and mock_mavros_service not implemented yet

# Install executables
install(TARGETS
  movement_action_client
  motion_coordinator
  drone_control_node
  gazebo_px4_bridge
  DESTINATION lib/${PROJECT_NAME}
)

# Install directories
install(DIRECTORY
  launch
  config
  worlds
  urdf
  crazyflie
  DESTINATION share/${PROJECT_NAME}/
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

# Export dependencies for downstream packages
ament_export_dependencies(rosidl_default_runtime)

ament_package()
