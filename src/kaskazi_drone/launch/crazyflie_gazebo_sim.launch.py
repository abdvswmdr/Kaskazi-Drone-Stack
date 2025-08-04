#!/usr/bin/env python3
"""
Launch file for Crazyflie Gazebo simulation.

This launch file:
1. Starts Gazebo with the crazyflie world
2. Spawns the Crazyflie drone model
3. Launches robot state publisher
4. Sets up the simulation environment

Usage:
ros2 launch kaskazi_drone crazyflie_gazebo_sim.launch.py
"""

import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, ExecuteProcess
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, Command
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    """Generate launch description for Crazyflie Gazebo simulation."""
    
    # Get package directories
    pkg_kaskazi_drone = get_package_share_directory('kaskazi_drone')
    pkg_gazebo_ros = get_package_share_directory('gazebo_ros')
    
    # Declare launch arguments
    world_arg = DeclareLaunchArgument(
        'world',
        default_value=os.path.join(pkg_kaskazi_drone, 'worlds', 'empty.world'),
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
        default_value='0.2',
        description='Z spawn position'
    )
    
    # URDF file path
    urdf_file = os.path.join(pkg_kaskazi_drone, 'urdf', 'crazyflie_gazebo_sim.urdf')
    
    # Robot description
    robot_description = Command(['xacro ', urdf_file])
    
    # Robot state publisher
    robot_state_publisher_node = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[
            {'robot_description': robot_description},
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
    
    # Gazebo server
    gazebo_server = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(pkg_gazebo_ros, 'launch', 'gzserver.launch.py')
        ),
        launch_arguments={
            'world': LaunchConfiguration('world'),
            'verbose': 'true'
        }.items()
    )
    
    # Gazebo client  
    gazebo_client = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(pkg_gazebo_ros, 'launch', 'gzclient.launch.py')
        )
    )
    
    # Spawn the robot
    spawn_robot = Node(
        package='gazebo_ros',
        executable='spawn_entity.py',
        name='spawn_crazyflie',
        output='screen',
        arguments=[
            '-entity', LaunchConfiguration('robot_name'),
            '-file', urdf_file,
            '-x', LaunchConfiguration('x_spawn'),
            '-y', LaunchConfiguration('y_spawn'),
            '-z', LaunchConfiguration('z_spawn'),
            '-R', '0.0',
            '-P', '0.0', 
            '-Y', '0.0'
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
        spawn_robot
    ])
