#!/usr/bin/env python3
"""
Simple Gazebo test without gazebo_ros dependency.
This tests if we can spawn the Crazyflie model directly.
"""

import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    """Generate simplified launch description."""
    
    pkg_kaskazi_drone = get_package_share_directory('kaskazi_drone')
    
    # URDF file path
    urdf_file = os.path.join(pkg_kaskazi_drone, 'urdf', 'crazyflie_gazebo_sim.urdf')
    world_file = os.path.join(pkg_kaskazi_drone, 'worlds', 'crazyflie_world.world')
    
    # Start Gazebo with world file
    gazebo_cmd = ExecuteProcess(
        cmd=['gazebo', '--verbose', world_file],
        output='screen'
    )
    
    # Robot state publisher
    with open(urdf_file, 'r') as f:
        robot_description = f.read()
    
    robot_state_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[{'robot_description': robot_description}]
    )
    
    return LaunchDescription([
        gazebo_cmd,
        robot_state_publisher
    ])