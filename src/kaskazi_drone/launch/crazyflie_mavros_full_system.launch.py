#!/usr/bin/env python3
"""
Complete Crazyflie MAVROS Integration Launch File

This launch file provides full MAVROS integration with the Crazyflie drone in Gazebo:
1. Gazebo Harmonic with enhanced Crazyflie model (flight control plugins)
2. MAVROS connection to ArduPilot SITL 
3. Gazebo-MAVROS bridge for seamless integration
4. Complete A* navigation system
5. Movement action client for testing

Architecture:
Movement Client → A* Coordinator → Drone Control → MAVROS → Bridge → Gazebo Crazyflie

Usage:
ros2 launch kaskazi_drone crazyflie_mavros_full_system.launch.py x_param:=3.0 y_param:=4.0 z_param:=5.0
"""

import os
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument, 
    ExecuteProcess, 
    TimerAction,
    IncludeLaunchDescription
)
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, Command
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    """Generate launch description for complete Crazyflie-MAVROS integration."""
    
    # Get package directories
    pkg_kaskazi_drone = get_package_share_directory('kaskazi_drone')
    
    # CRITICAL: Set GZ_SIM_RESOURCE_PATH for mesh loading
    gazebo_models_path, ignore_last_dir = os.path.split(pkg_kaskazi_drone)
    if "GZ_SIM_RESOURCE_PATH" in os.environ:
        os.environ["GZ_SIM_RESOURCE_PATH"] += os.pathsep + gazebo_models_path
    else:
        os.environ["GZ_SIM_RESOURCE_PATH"] = gazebo_models_path
    
    # Launch arguments for movement goals
    x_arg = DeclareLaunchArgument(
        'x_param',
        default_value='3.0',
        description='Target X coordinate for movement (meters)'
    )
    
    y_arg = DeclareLaunchArgument(
        'y_param', 
        default_value='4.0',
        description='Target Y coordinate for movement (meters)'
    )
    
    z_arg = DeclareLaunchArgument(
        'z_param',
        default_value='5.0', 
        description='Target Z coordinate for movement (meters)'
    )
    
    use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time',
        default_value='true',
        description='Use simulation time'
    )
    
    # URDF file path - use enhanced Crazyflie with flight control
    urdf_file = os.path.join(pkg_kaskazi_drone, 'urdf', 'crazyflie_mavros.urdf')
    
    # 1. Robot State Publisher with enhanced Crazyflie URDF
    robot_state_publisher_node = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[
            {'robot_description': Command(['cat', ' ', urdf_file])},
            {'use_sim_time': LaunchConfiguration('use_sim_time')}
        ]
    )
    
    # 2. Joint State Publisher
    joint_state_publisher_node = Node(
        package='joint_state_publisher',
        executable='joint_state_publisher',
        name='joint_state_publisher',
        output='screen',
        parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}]
    )
    
    # 3. Start Gazebo Harmonic server
    gazebo_server = ExecuteProcess(
        cmd=[
            'gz', 'sim', 
            '-r',  # Run server only 
            '-s',  # Run headless (remove this for GUI)
            os.path.join(pkg_kaskazi_drone, 'worlds', 'empty.sdf')
        ],
        output='screen',
        name='gz_sim_server'
    )
    
    # 4. Start Gazebo Harmonic client (GUI) - optional
    gazebo_client = ExecuteProcess(
        cmd=['gz', 'sim', '-g'],  # GUI only
        output='screen',
        name='gz_sim_client'
    )
    
    # 5. Spawn the Crazyflie robot in Gazebo
    spawn_robot = TimerAction(
        period=3.0,  # Wait for Gazebo to start
        actions=[
            Node(
                package='ros_gz_sim',
                executable='create',
                name='spawn_crazyflie',
                output='screen',
                arguments=[
                    '-name', 'crazyflie',
                    '-topic', 'robot_description',
                    '-x', '0.0',
                    '-y', '0.0',
                    '-z', '0.5',  # Start hovering
                ],
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}]
            )
        ]
    )
    
    # 6. ROS-Gazebo bridge for communication
    ros_gz_bridge = TimerAction(
        period=4.0,
        actions=[
            Node(
                package='ros_gz_bridge',
                executable='parameter_bridge',
                name='ros_gz_bridge',
                output='screen',
                arguments=[
                    '/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock',
                    '/crazyflie/cmd_vel@geometry_msgs/msg/Twist@gz.msgs.Twist',
                    '/crazyflie/enable@std_msgs/msg/Bool@gz.msgs.Boolean',
                    '/odom@nav_msgs/msg/Odometry@gz.msgs.Odometry',
                    '/imu@sensor_msgs/msg/Imu@gz.msgs.IMU',
                    '/joint_states@sensor_msgs/msg/JointState@gz.msgs.Model',
                ],
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}]
            )
        ]
    )
    
    # 7. Start ArduPilot SITL
    ardupilot_sitl = TimerAction(
        period=5.0,
        actions=[
            ExecuteProcess(
                cmd=[
                    'bash', '-c',
                    'cd /home/abdvswmdr/ardupilot/ArduCopter && ' +
                    '../Tools/autotest/sim_vehicle.py ' +
                    '--vehicle ArduCopter ' +
                    '--frame quad ' +
                    '--console --map --no-rebuild'
                ],
                output='screen',
                name='ardupilot_sitl',
                shell=True
            )
        ]
    )
    
    # 8. Start MAVROS (connects ArduPilot to ROS2)
    mavros_node = TimerAction(
        period=10.0,  # Wait for ArduPilot SITL to start
        actions=[
            Node(
                package='mavros',
                executable='mavros_node',
                name='mavros',
                output='screen',
                parameters=[
                    {'fcu_url': 'udp://:14550@'},
                    {'gcs_url': ''},
                    {'target_system_id': 1},
                    {'target_component_id': 1},
                    {'use_sim_time': LaunchConfiguration('use_sim_time')}
                ],
                emulate_tty=True
            )
        ]
    )
    
    # 9. Gazebo-MAVROS Bridge (crucial component!)
    gazebo_mavros_bridge = TimerAction(
        period=12.0,  # Start after MAVROS is ready
        actions=[
            Node(
                package='kaskazi_drone',
                executable='gazebo_mavros_bridge',
                name='gazebo_mavros_bridge',
                output='screen',
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}],
                emulate_tty=True
            )
        ]
    )
    
    # 10. Motion Coordinator Node (A* planning + action server)
    motion_coordinator_node = TimerAction(
        period=13.0,  # Start after bridge is ready
        actions=[
            Node(
                package='kaskazi_drone',
                executable='motion_coordinator',
                name='motion_coordinator',
                output='screen',
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}],
                emulate_tty=True
            )
        ]
    )
    
    # 11. Real Drone Control Node (MAVROS integration)
    drone_control_node = TimerAction(
        period=14.0,  # Start after motion coordinator
        actions=[
            Node(
                package='kaskazi_drone',
                executable='drone_control_node',
                name='drone_control_node',
                output='screen',
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}],
                emulate_tty=True
            )
        ]
    )
    
    # 12. Movement Action Client (sends goals after all services are ready)
    movement_client_node = TimerAction(
        period=18.0,  # Wait for all services to start
        actions=[
            Node(
                package='kaskazi_drone',
                executable='movement_action_client',
                name='movement_action_client',
                output='screen',
                parameters=[
                    {'x_param': LaunchConfiguration('x_param')},
                    {'y_param': LaunchConfiguration('y_param')},
                    {'z_param': LaunchConfiguration('z_param')},
                    {'use_sim_time': LaunchConfiguration('use_sim_time')}
                ],
                emulate_tty=True
            )
        ]
    )
    
    return LaunchDescription([
        # Launch arguments
        x_arg,
        y_arg, 
        z_arg,
        use_sim_time_arg,
        
        # Core simulation components
        robot_state_publisher_node,  # URDF publisher
        joint_state_publisher_node,  # Joint states
        gazebo_server,               # Gazebo physics
        gazebo_client,               # Gazebo GUI
        spawn_robot,                 # Spawn Crazyflie
        ros_gz_bridge,               # ROS-Gazebo communication
        
        # Flight control stack
        ardupilot_sitl,              # ArduPilot SITL flight controller
        mavros_node,                 # MAVROS interface
        gazebo_mavros_bridge,        # Bridge: MAVROS ↔ Gazebo
        
        # Navigation stack
        motion_coordinator_node,     # A* path planning
        drone_control_node,          # MAVROS waypoint interface
        movement_client_node,        # Goal sender
    ])