#!/usr/bin/env python3
"""
Simple Crazyflie Gazebo Test Launch File

This launch file tests basic Crazyflie functionality in Gazebo:
1. Gazebo Harmonic with enhanced Crazyflie model
2. Basic flight control via cmd_vel topics
3. A* motion planning system

Usage:
ros2 launch kaskazi_drone crazyflie_gazebo_test.launch.py x_param:=2.0 y_param:=3.0 z_param:=4.0
"""

import os
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument, 
    ExecuteProcess, 
    TimerAction
)
from launch.substitutions import LaunchConfiguration, Command
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    """Generate launch description for Crazyflie Gazebo testing."""
    
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
        default_value='2.0',
        description='Target X coordinate for movement (meters)'
    )
    
    y_arg = DeclareLaunchArgument(
        'y_param', 
        default_value='3.0',
        description='Target Y coordinate for movement (meters)'
    )
    
    z_arg = DeclareLaunchArgument(
        'z_param',
        default_value='4.0', 
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
            '-r',  # Run server
            os.path.join(pkg_kaskazi_drone, 'worlds', 'empty.sdf')
        ],
        output='screen',
        name='gz_sim_server'
    )
    
    # 4. Start Gazebo Harmonic client (GUI)
    gazebo_client = ExecuteProcess(
        cmd=['gz', 'sim', '-g'],  # GUI only
        output='screen',
        name='gz_sim_client'
    )
    
    # 5. Spawn the Crazyflie robot in Gazebo (using robot_description topic)
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
                    '-topic', 'robot_description',  # Key: use topic not file
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
                    '/crazyflie/cmd_vel@geometry_msgs/msg/Twist]gz.msgs.Twist',
                    '/crazyflie/enable@std_msgs/msg/Bool]gz.msgs.Boolean',
                    '/odom@nav_msgs/msg/Odometry[gz.msgs.Odometry',
                    '/imu@sensor_msgs/msg/Imu[gz.msgs.IMU',
                    '/joint_states@sensor_msgs/msg/JointState[gz.msgs.Model',
                ],
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}]
            )
        ]
    )
    
    # 7. Motion Coordinator Node (A* planning + action server)
    motion_coordinator_node = TimerAction(
        period=6.0,  # Start after bridge is ready
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
    
    # 8. Simple velocity bridge for testing (instead of MAVROS complexity)
    test_cmd_vel_publisher = TimerAction(
        period=8.0,  # Start after everything is ready
        actions=[
            Node(
                package='kaskazi_drone',
                executable='gazebo_mavros_bridge',
                name='test_velocity_bridge',
                output='screen',
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}],
                emulate_tty=True
            )
        ]
    )
    
    # 9. Movement Action Client (sends goals for testing)
    movement_client_node = TimerAction(
        period=10.0,  # Wait for all services to start
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
        spawn_robot,                 # Spawn Crazyflie via topic
        ros_gz_bridge,               # ROS-Gazebo communication
        
        # Navigation and control
        motion_coordinator_node,     # A* path planning
        test_cmd_vel_publisher,      # Simple velocity control
        movement_client_node,        # Goal sender for testing
    ])