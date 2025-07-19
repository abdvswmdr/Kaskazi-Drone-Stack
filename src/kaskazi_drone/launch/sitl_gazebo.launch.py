#!/usr/bin/env python3

"""
Kaskazi Drone Launch File
Integrates ArduPilot SITL + Gazebo + ROS2 DDS

Usage:
  ros2 launch kaskazi_drone sitl_gazebo.launch.py
  ros2 launch kaskazi_drone sitl_gazebo.launch.py vehicle:=plane
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess, IncludeLaunchDescription
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare
from launch.launch_description_sources import PythonLaunchDescriptionSource


def generate_launch_description():
    # Launch arguments
    vehicle_arg = DeclareLaunchArgument(
        'vehicle',
        default_value='ArduCopter',
        description='Vehicle type: ArduCopter, ArduPlane, ArduRover'
    )
    
    frame_arg = DeclareLaunchArgument(
        'frame',
        default_value='iris',
        description='Frame type: iris, quad, plane, rover'
    )
    
    world_arg = DeclareLaunchArgument(
        'world',
        default_value='empty.world',
        description='Gazebo world file'
    )
    
    # Get launch configurations
    vehicle = LaunchConfiguration('vehicle')
    frame = LaunchConfiguration('frame')
    world = LaunchConfiguration('world')
    
    # ArduPilot SITL with DDS
    ardupilot_sitl = ExecuteProcess(
        cmd=[
            'bash', '-c',
            f'cd ~/ardupilot/ArduCopter && ' +
            f'../Tools/autotest/sim_vehicle.py ' +
            f'--vehicle {vehicle} ' +
            f'--frame {frame} ' +
            f'--console --map --dds'
        ],
        output='screen',
        shell=True
    )
    
    # Micro-ROS Agent for DDS communication  
    micro_ros_agent = ExecuteProcess(
        cmd=['ros2', 'run', 'micro_ros_agent', 'micro_ros_agent', 'udp4', '--port', '2019'],
        output='screen'
    )
    
    # Gazebo simulation (commented out initially - add when Gazebo plugin ready)
    # gazebo_sim = IncludeLaunchDescription(
    #     PythonLaunchDescriptionSource([
    #         PathJoinSubstitution([
    #             FindPackageShare('gazebo_ros'),
    #             'launch',
    #             'gazebo.launch.py'
    #         ])
    #     ]),
    #     launch_arguments={'world': world}.items()
    # )
    
    return LaunchDescription([
        vehicle_arg,
        frame_arg,
        world_arg,
        
        # Core components
        ardupilot_sitl,
        micro_ros_agent,
        
        # Gazebo (enable when plugin configured)
        # gazebo_sim,
    ])