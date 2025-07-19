#!/usr/bin/env python3

"""
Gazebo Iris Launch File
Basic Gazebo simulation with Iris quadcopter model

Usage:
  ros2 launch kaskazi_drone gazebo_iris.launch.py
"""

import os
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
        default_value=[FindPackageShare('kaskazi_drone'), '/worlds/empty_ground.world'],
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
        output='screen',
        additional_env={'GZ_SIM_RESOURCE_PATH': f'{os.path.expanduser("~/kaskazi_ws/src/ardupilot_gazebo/models")}:{os.environ.get("GZ_SIM_RESOURCE_PATH", "")}'}
    )
    
    # Gazebo client (GUI)
    gazebo_client = ExecuteProcess(
        cmd=['gz', 'sim', '-g'],
        output='screen',
        condition=IfCondition(gui)
    )
    
    # Spawn Iris quadcopter model
    spawn_iris = ExecuteProcess(
        cmd=[
            'gz', 'service', '-s', '/world/empty_ground/create',
            '--reqtype', 'gz.msgs.EntityFactory',
            '--reptype', 'gz.msgs.Boolean',
            '--timeout', '5000',
            '--req', 'sdf_filename: "iris_with_ardupilot", name: "iris", pose: {position: {x: 0, y: 0, z: 0.1}}'
        ],
        output='screen',
        additional_env={'GZ_SIM_RESOURCE_PATH': f'{os.path.expanduser("~/kaskazi_ws/src/ardupilot_gazebo/models")}:{os.environ.get("GZ_SIM_RESOURCE_PATH", "")}'}
    )
    
    return LaunchDescription([
        world_arg,
        gui_arg,
        
        # Gazebo simulation
        gazebo_server,
        gazebo_client,
        
        # Model spawning
        spawn_iris,
    ])