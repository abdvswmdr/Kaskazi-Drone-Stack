#!/usr/bin/env python3
"""
Launch file for Crazyflie Gazebo Harmonic simulation.

This launch file:
1. Starts Gazebo Harmonic with the crazyflie world
2. Spawns the Crazyflie drone model
3. Launches robot state publisher
4. Sets up the simulation environment

Usage:
ros2 launch kaskazi_drone crazyflie_gazebo_harmonic.launch.py
"""

import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, ExecuteProcess, TimerAction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, Command
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    """Generate launch description for Crazyflie Gazebo Harmonic simulation."""
    
    # Get package directories
    pkg_kaskazi_drone = get_package_share_directory('kaskazi_drone')
    
    # Set GZ_SIM_RESOURCE_PATH for Gazebo to find package resources
    gazebo_models_path, ignore_last_dir = os.path.split(pkg_kaskazi_drone)
    if "GZ_SIM_RESOURCE_PATH" in os.environ:
        os.environ["GZ_SIM_RESOURCE_PATH"] += os.pathsep + gazebo_models_path
    else:
        os.environ["GZ_SIM_RESOURCE_PATH"] = gazebo_models_path
    
    # Declare launch arguments
    world_arg = DeclareLaunchArgument(
        'world',
        default_value=os.path.join(pkg_kaskazi_drone, 'worlds', 'empty.sdf'),
        description='Path to world file'
    )
    
    use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time',
        default_value='true',
        description='Use simulation time'
    )
    
    robot_name_arg = DeclareLaunchArgument(
        'robot_name',
        default_value='crazyflie',
        description='Name of the robot'
    )
    
    x_spawn_arg = DeclareLaunchArgument(
        'x_spawn',
        default_value='0.0',
        description='X spawn position'
    )
    
    y_spawn_arg = DeclareLaunchArgument(
        'y_spawn', 
        default_value='0.0',
        description='Y spawn position'
    )
    
    z_spawn_arg = DeclareLaunchArgument(
        'z_spawn',
        default_value='0.5',
        description='Z spawn position'
    )
    
    # URDF file path
    urdf_file = os.path.join(pkg_kaskazi_drone, 'urdf', 'crazyflie_ros2.urdf')
    
    # Robot state publisher - using URDF file directly
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
    
    # Joint state publisher (for visualization)
    joint_state_publisher_node = Node(
        package='joint_state_publisher',
        executable='joint_state_publisher',
        name='joint_state_publisher',
        output='screen',
        parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}]
    )
    
    # Start Gazebo Harmonic server
    gazebo_server = ExecuteProcess(
        cmd=[
            'gz', 'sim', 
            '-r',  # Run server only 
            '-s',  # Run headless
            LaunchConfiguration('world')
        ],
        output='screen',
        name='gz_sim_server'
    )
    
    # Start Gazebo Harmonic client (GUI)
    gazebo_client = ExecuteProcess(
        cmd=['gz', 'sim', '-g'],  # GUI only
        output='screen',
        name='gz_sim_client'
    )
    
    # Spawn the robot using robot_description topic (like working fyp_nav)
    spawn_robot = TimerAction(
        period=3.0,  # Wait for Gazebo to start
        actions=[
            Node(
                package='ros_gz_sim',
                executable='create',
                name='spawn_crazyflie',
                output='screen',
                arguments=[
                    '-name', LaunchConfiguration('robot_name'),
                    '-topic', 'robot_description',
                    '-x', LaunchConfiguration('x_spawn'),
                    '-y', LaunchConfiguration('y_spawn'),
                    '-z', LaunchConfiguration('z_spawn'),
                ],
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}]
            )
        ]
    )
    
    # ROS-Gazebo bridge for communication
    ros_gz_bridge = Node(
        package='ros_gz_bridge',
        executable='parameter_bridge',
        name='ros_gz_bridge',
        output='screen',
        arguments=[
            '/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock',
            '/world/default/model/crazyflie/joint_state@sensor_msgs/msg/JointState[gz.msgs.Model',
        ],
        parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}]
    )
    
    return LaunchDescription([
        world_arg,
        use_sim_time_arg,
        robot_name_arg,
        x_spawn_arg,
        y_spawn_arg,
        z_spawn_arg,
        robot_state_publisher_node,
        joint_state_publisher_node,
        gazebo_server,
        gazebo_client,
        spawn_robot,
        ros_gz_bridge
    ])