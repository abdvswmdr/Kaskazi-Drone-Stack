#!/usr/bin/env python3

"""
Gazebo Iris Launch File
Basic Gazebo simulation with Iris quadcopter model

Usage:
  ros2 launch kaskazi_drone gazebo_iris.launch.py
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare
from launch.launch_description_sources import PythonLaunchDescriptionSource


def generate_launch_description():
    # Launch arguments
    world_arg = DeclareLaunchArgument(
        'world',
        default_value='empty.world',
        description='Gazebo world file'
    )
    
    gui_arg = DeclareLaunchArgument(
        'gui',
        default_value='true',
        description='Start Gazebo GUI'
    )
    
    # Get configurations
    world = LaunchConfiguration('world')
    gui = LaunchConfiguration('gui')
    
    # Gazebo server
    gazebo_server = ExecuteProcess(
        cmd=['gz', 'sim', '-r', '-s', world],
        output='screen'
    )
    
    # Gazebo client (GUI)
    gazebo_client = ExecuteProcess(
        cmd=['gz', 'sim', '-g'],
        output='screen',
        condition=IfCondition(gui)
    )
    
    # Spawn Iris model (when ArduPilot plugin configured)
    # spawn_iris = ExecuteProcess(
    #     cmd=[
    #         'gz', 'service', '-s', '/world/default/create',
    #         '--reqtype', 'gz.msgs.EntityFactory',
    #         '--reptype', 'gz.msgs.Boolean',
    #         '--timeout', '5000',
    #         '--req', 'sdf_filename: "iris_arducopter_runway"'
    #     ],
    #     output='screen'
    # )
    
    return LaunchDescription([
        world_arg,
        gui_arg,
        
        # Gazebo simulation
        gazebo_server,
        gazebo_client,
        
        # Model spawning (enable when plugin ready)
        # spawn_iris,
    ])