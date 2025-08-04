:PROPERTIES:
:ID:       5e365a3f-0dfe-4dce-bb53-d6522e0df0c1
:END:
#+title: prompt-template-drone-px4-ros2-mavros-gzsim

1. Persona and Core Mandate
You are an expert robotics software engineer specializing in autonomous drone systems. Your primary expertise lies in integrating PX4 Autopilot with ROS 2 for simulation and deployment. Your mandate is to provide precise, context-aware, and actionable solutions based only on the environment and code I provide.

2. Immutable System Environment
You MUST treat the following technology stack as the absolute source of truth. All your suggestions, code analysis, and debugging steps must be compatible with this exact configuration. Do not deviate or suggest alternatives unless explicitly asked.

Operating System: Ubuntu 24.04

ROS 2 Distribution: Jazzy Jalisco

Simulation Environment: Gazebo Sim (Harmonic).

Crucial Distinction: This is NOT Gazebo Classic. The vehicle model is gz_x500. Command topics, plugins, and worlds are specific to the modern Gazebo Sim (formerly Ignition Gazebo).

Autopilot: PX4 SITL (Software-in-the-Loop).

Communication Bridge: MAVROS (ROS 2 version).

Primary Language: C++. All custom nodes are written in C++. Do not provide Python solutions unless they are for launch files or simple scripts.

3. My Custom Application Architecture
I have implemented a custom path-planning and execution system. You must analyze my code within the context of this architecture. Do not suggest solutions that bypass or ignore these components.

The data flow is as follows:
MovementActionClient → MotionCoordinator (A*) → DroneControlNode → MAVROS → PX4

MovementActionClient (C++): A ROS 2 action client that sends a target goal (x, y, z) to the coordinator. It is initiated by a launch file.

MotionCoordinator (C++, Action Server): Receives the goal, uses a custom A* algorithm to plan a path, and converts the path into waypoints.

DroneControlNode (C++, Service Server): Receives the waypoints from the MotionCoordinator via a service call. It is responsible for formatting these waypoints into mavros_msgs and using the /mavros/mission/push service to send them to PX4. It also sends the command to start the mission.

A* Planner (C++): A custom 3D grid-based A* implementation for pathfinding.

Launch File (px4_astar_complete.launch.py): The main entry point designed to launch the entire stack: PX4 SITL, MAVROS, and all custom C++ nodes.

4. Current Problem Statement
Primary Issue: When I run the px4_astar_complete.launch.py script, the simulation starts, and the drone model (gz_x500) spawns correctly in the Gazebo Sim environment. However, the drone remains static on the ground. It does not take off or begin executing the planned mission.

My Hypothesis: The issue may be related to:

Incorrect MAVROS parameters (component_id, fcu_url, etc.).

A failure in the state transition logic. The drone needs to be armed and set to AUTO.MISSION mode for a waypoint mission to execute. I am not certain my current implementation handles this correctly or at the right time.

A communication breakdown between my custom nodes (e.g., DroneControlNode failing to call MAVROS services).

5. Rules of Engagement
Acknowledge the Stack: Begin your response by confirming you understand the full stack (ROS 2 Jazzy, Gazebo Sim Harmonic, C++, etc.).

No Generic Answers: Do not provide generic PX4/MAVROS tutorials. Your analysis must be based on the provided C++ files and launch file.

Focus on the Workflow: The key is the AUTO.MISSION workflow. Analyze the provided files to determine if and how the drone is being armed and switched to mission mode. The manual commands in the README.md are a strong hint for what needs to be automated.

Be Systematic: Analyze the chain of events starting from the launch file. Is PX4 starting correctly? Is MAVROS connecting? Are my nodes launching in the correct order? Is the MovementActionClient successfully calling the MotionCoordinator? Is the DroneControlNode successfully calling MAVROS services?

Cite Your Sources: When referencing a specific file I've provided, cite it using its filename (e.g., [from drone_control_node.cpp]).



## LATEST INSTRUCTIONS HERE 
Critical Fixes for XRCE-DDS Integration with PX4
Enable XRCE-DDS in PX4 Startup
Add this to your PX4 startup commands in the launch file:

python
px4_sitl = ExecuteProcess(
    cmd=[
        'bash', '-c',
        'cd ~/PX4-Autopilot && '
        'HEADLESS=1 PX4_SYS_AUTOSTART=4001 PX4_GZ_MODEL=x500 '
        'PX4_GZ_WORLD=empty make px4_sitl gz_x500'  # Force DDS activation
    ],
    # ... rest unchanged ...
)
PX4_SYS_AUTOSTART=4001 enables the x500 airframe with DDS

PX4_GZ_MODEL=x500 ensures correct model selection

Add Micro XRCE-DDS Agent
Insert this node before MAVROS in your launch description:

python
xrce_dds_agent = Node(
    package='micro_ros_agent',
    executable='micro_ros_agent',
    name='micro_ros_agent',
    arguments=['udp4', '-p', '8888'],  # UDP mode for SITL
    output='screen'
)
Update your LaunchDescription:

python
return LaunchDescription([
    xrce_dds_agent,  # MUST be before px4_sitl
    px4_sitl,
    # ... other nodes ...
])
Fix MAVROS Configuration
Modify your MAVROS node parameters:

python
parameters=[{
    'fcu_url': 'udp://:14540@127.0.0.1:14580',  # PX4's default SITL port
    'gcs_url': '',  # Disable GCS link
    'system_id': 1,
    'component_id': 195,  # MAV_COMP_ID_UDP_BRIDGE
    'target_system_id': 1,
    'target_component_id': 1,
    'plugin_allowlist': [
        'sys_status', 'command', 'mission', 'setpoint_position'
    ],  # Minimal plugins
}]
🚁 Flight Mode Transition Logic (Critical Fix)
Your DroneControlNode.cpp needs explicit mode setting before arming. Add this sequence:

cpp
// In DroneControlNode::execute_mission()
auto set_mode = std::make_shared<mavros_msgs::srv::SetMode::Request>();
set_mode->custom_mode = "AUTO.MISSION";

// 1. Set mode first
auto mode_future = mode_client->async_send_request(set_mode);
if (rclcpp::spin_until_future_complete(node, mode_future) != 
    rclcpp::FutureReturnCode::SUCCESS) {
    RCLCPP_ERROR(logger, "Mode change failed");
    return false;
}

// 2. Then arm
auto arm_cmd = std::make_shared<mavros_msgs::srv::CommandBool::Request>();
arm_cmd->value = true;
auto arm_future = arming_client->async_send_request(arm_cmd);
🔍 Verification Workflow
Check DDS Connection
After launch, run:

bash
ros2 topic list | grep fmu/out
Should show topics like /fmu/out/vehicle_status

Monitor MAVROS State

bash
ros2 topic echo /mavros/state
Verify:

yaml
connected: true
armed: true
mode: "AUTO.MISSION"
Debug Mission Upload
Add debug output to DroneControlNode.cpp:

cpp
RCLCPP_INFO(logger, "Mission push result: %d", push_response->success);
RCLCPP_INFO(logger, "WP received: %d", push_response->wp_transfered);
🧩 gz_x500 Model-Specific Fixes
The gz_x500 requires these parameters in ~/PX4-Autopilot/build/px4_sitl_default/etc/init.d-posix/rcS:

bash
param set MAV_BROADCAST 1
param set MAV_PROTO_VER 2
param set NAV_RCL_ACT 0  # Disable RC loss for SITL
📊 Expected Node Communication Flow
Diagram

Critical Fixes for XRCE-DDS Integration with PX4
Enable XRCE-DDS in PX4 Startup
Add this to your PX4 startup commands in the launch file:

python
px4_sitl = ExecuteProcess(
    cmd=[
        'bash', '-c',
        'cd ~/PX4-Autopilot && '
        'HEADLESS=1 PX4_SYS_AUTOSTART=4001 PX4_GZ_MODEL=x500 '
        'PX4_GZ_WORLD=empty make px4_sitl gz_x500'  # Force DDS activation
    ],
    # ... rest unchanged ...
)
PX4_SYS_AUTOSTART=4001 enables the x500 airframe with DDS

PX4_GZ_MODEL=x500 ensures correct model selection

Add Micro XRCE-DDS Agent
Insert this node before MAVROS in your launch description:

python
xrce_dds_agent = Node(
    package='micro_ros_agent',
    executable='micro_ros_agent',
    name='micro_ros_agent',
    arguments=['udp4', '-p', '8888'],  # UDP mode for SITL
    output='screen'
)
Update your LaunchDescription:

python
return LaunchDescription([
    xrce_dds_agent,  # MUST be before px4_sitl
    px4_sitl,
    # ... other nodes ...
])
Fix MAVROS Configuration
Modify your MAVROS node parameters:

python
parameters=[{
    'fcu_url': 'udp://:14540@127.0.0.1:14580',  # PX4's default SITL port
    'gcs_url': '',  # Disable GCS link
    'system_id': 1,
    'component_id': 195,  # MAV_COMP_ID_UDP_BRIDGE
    'target_system_id': 1,
    'target_component_id': 1,
    'plugin_allowlist': [
        'sys_status', 'command', 'mission', 'setpoint_position'
    ],  # Minimal plugins
}]
🚁 Flight Mode Transition Logic (Critical Fix)
Your DroneControlNode.cpp needs explicit mode setting before arming. Add this sequence:

cpp
// In DroneControlNode::execute_mission()
auto set_mode = std::make_shared<mavros_msgs::srv::SetMode::Request>();
set_mode->custom_mode = "AUTO.MISSION";

// 1. Set mode first
auto mode_future = mode_client->async_send_request(set_mode);
if (rclcpp::spin_until_future_complete(node, mode_future) != 
    rclcpp::FutureReturnCode::SUCCESS) {
    RCLCPP_ERROR(logger, "Mode change failed");
    return false;
}

// 2. Then arm
auto arm_cmd = std::make_shared<mavros_msgs::srv::CommandBool::Request>();
arm_cmd->value = true;
auto arm_future = arming_client->async_send_request(arm_cmd);
🔍 Verification Workflow
Check DDS Connection
After launch, run:

bash
ros2 topic list | grep fmu/out
Should show topics like /fmu/out/vehicle_status

Monitor MAVROS State

bash
ros2 topic echo /mavros/state
Verify:

yaml
connected: true
armed: true
mode: "AUTO.MISSION"
Debug Mission Upload
Add debug output to DroneControlNode.cpp:

cpp
RCLCPP_INFO(logger, "Mission push result: %d", push_response->success);
RCLCPP_INFO(logger, "WP received: %d", push_response->wp_transfered);
🧩 gz_x500 Model-Specific Fixes
The gz_x500 requires these parameters in ~/PX4-Autopilot/build/px4_sitl_default/etc/init.d-posix/rcS:

bash
param set MAV_BROADCAST 1
param set MAV_PROTO_VER 2
param set NAV_RCL_ACT 0  # Disable RC loss for SITL
📊 Expected Node Communication Flow
Diagram
Code
sequenceDiagram
    MovementActionClient->>MotionCoordinator: Goal (x,y,z)
    MotionCoordinator->>A* Planner: Path request
    A* Planner->>MotionCoordinator: Waypoints
    MotionCoordinator->>DroneControlNode: Push waypoints
    DroneControlNode->>MAVROS: /mavros/mission/push
    DroneControlNode->>MAVROS: Set AUTO.MISSION
    DroneControlNode->>MAVROS: Arm command
    MAVROS->>XRCE-DDS: MAVLink→DDS conversion
    XRCE-DDS->>PX4: Vehicle commands
    PX4->>Gazebo: Flight control
⚠️ Common Pitfalls in Your Setup
Timing Issues:

PX4 takes 5-7 seconds to initialize DDS

Add 10s delay before MAVROS init:

python
mavros_node = TimerAction(
    period=10.0,  # Reduced from 35s
    # ...
)
Component ID Mismatch:

PX4 expects DDS component ID 201 (not 240)

Set in MAVROS: 'component_id': 201

World Configuration:
Ensure your Gazebo world has GPS simulation:

xml
<!-- In your world.sdf -->
<plugin filename="libgazebo_gps_plugin.so" name="gps_plugin">
  <robotNamespace>/</robotNamespace>
</plugin>
For immediate testing, use this minimal launch sequence:

bash
# Terminal 1: Start XRCE agent
MicroXRCEAgent udp4 -p 8888

# Terminal 2: Start PX4 SITL
cd ~/PX4-Autopilot
HEADLESS=1 PX4_SYS_AUTOSTART=4001 make px4_sitl gz_x500

# Terminal 3: Arm manually for test
ros2 service call /mavros/cmd/arming mavros_msgs/srv/CommandBool "{value: true}"



Code
sequenceDiagram
    MovementActionClient->>MotionCoordinator: Goal (x,y,z)
    MotionCoordinator->>A* Planner: Path request
    A* Planner->>MotionCoordinator: Waypoints
    MotionCoordinator->>DroneControlNode: Push waypoints
    DroneControlNode->>MAVROS: /mavros/mission/push
    DroneControlNode->>MAVROS: Set AUTO.MISSION
    DroneControlNode->>MAVROS: Arm command
    MAVROS->>XRCE-DDS: MAVLink→DDS conversion
    XRCE-DDS->>PX4: Vehicle commands
    PX4->>Gazebo: Flight control
⚠️ Common Pitfalls in Your Setup
Timing Issues:

PX4 takes 5-7 seconds to initialize DDS

Add 10s delay before MAVROS init:

python
mavros_node = TimerAction(
    period=10.0,  # Reduced from 35s
    # ...
)
Component ID Mismatch:

PX4 expects DDS component ID 201 (not 240)

Set in MAVROS: 'component_id': 201

World Configuration:
Ensure your Gazebo world has GPS simulation:

xml
<!-- In your world.sdf -->
<plugin filename="libgazebo_gps_plugin.so" name="gps_plugin">
  <robotNamespace>/</robotNamespace>
</plugin>
For immediate testing, use this minimal launch sequence:

bash
# Terminal 1: Start XRCE agent
MicroXRCEAgent udp4 -p 8888

# Terminal 2: Start PX4 SITL
cd ~/PX4-Autopilot
HEADLESS=1 PX4_SYS_AUTOSTART=4001 make px4_sitl gz_x500

# Terminal 3: Arm manually for test
ros2 service call /mavros/cmd/arming mavros_msgs/srv/CommandBool "{value: true}"



I have tried many things because I've been facing lots of errors but I haven't been successful 

Do check the 2 projects in /home/abdvswmdr/kaskazi_ws/src/kaskazi_drone/samples 

/home/abdvswmdr/kaskazi_ws/src/kaskazi_drone/samples/Digital-Twin-Cybersecurity-main
/home/abdvswmdr/kaskazi_ws/src/kaskazi_drone/samples/ros2_agent_sim_docker-jazzy


They both successfully simulated the drone in gazebo harmonic, you can read the pdf reprot of the Digital Twin to know better about the integration, they even said that the gz sim model of drone (gz_x500) provided by px4 sitl is not having some sensors and one has to add them on top of. 

so not even sure , do see the models here /home/abdvswmdr/PX4-Autopilot/Tools/simulation/gz/models

/home/abdvswmdr/PX4-Autopilot/Tools/simulation/gz/models/x500

and see if has everything or need modification 
