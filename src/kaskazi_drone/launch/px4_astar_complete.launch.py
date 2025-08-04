#!/usr/bin/env python3
"""
Complete ArduPilot + MAVROS + A* Path Planning Launch File

CORRECTED version for proper ArduPilot SITL integration with Gazebo Sim (Harmonic)
"""

import os
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument, ExecuteProcess, TimerAction
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    """Generate complete launch description for PX4 A* path planning system."""
    
    # Get package share directory
    pkg_share = FindPackageShare('kaskazi_drone')
    
    # Declare launch arguments for target coordinates
    x_arg = DeclareLaunchArgument(
        'x_param',
        default_value='15.0', 
        description='Target X coordinate for movement (meters)'
    )
    
    y_arg = DeclareLaunchArgument(
        'y_param',
        default_value='15.0', 
        description='Target Y coordinate for movement (meters)'
    )
    
    z_arg = DeclareLaunchArgument(
        'z_param',
        default_value='15.0', 
        description='Target Z coordinate for movement (meters)'
    )
    
    # PX4 SITL launch - Simple direct approach
    px4_sitl = ExecuteProcess(
        cmd=[
            'bash', '-c',
            'cd ~/PX4-Autopilot && '
            'HEADLESS=1 make px4_sitl gz_x500'  # Use stable gazebo target
        ],
        name='px4_sitl',
        output='screen',
        shell=False
    )

    # No separate Gazebo launch needed - PX4 will start Gazebo automatically
    # PX4's make target handles both PX4 SITL and Gazebo Sim integration
    
    # MAVROS node - bridges PX4 and ROS2 (FIXED PARAMETERS)
    mavros_node = TimerAction(
        period=35.0,  # Increased wait time for proper PX4 initialization
        actions=[
            Node(
                package='mavros',
                executable='mavros_node',
                name='mavros',
                output='screen',
                parameters=[{
                    'fcu_url': 'udp://:14540@127.0.0.1:14557',
                    'system_id': 255,
                    'component_id': 240,  # Use component ID 240 (standard for companion computers)
                    'target_system_id': 1,
                    'target_component_id': 1,
                    'frame_id': 'map',
                    'fcu_frame_id': 'base_link_frd',
                    # Enable GPS plugin explicitly
                    'plugin_allowlist': ['sys_status', 'sys_time', 'command', 'setpoint_position', 'setpoint_velocity', 'local_position', 'global_position', 'imu', 'waypoint'],
                    'conn_timeout': 30.0
                }],
                emulate_tty=True
            )
        ]
    )
    
    # Gazebo-MAVROS Bridge - NEW COMPONENT for simulation control
    gazebo_bridge = TimerAction(
        period=45.0,  # Start after MAVROS is connected
        actions=[
            Node(
                package='kaskazi_drone',
                executable='gazebo_mavros_bridge',
                name='gazebo_mavros_bridge',
                output='screen',
                parameters=[],
                emulate_tty=True
            )
        ]
    )
    
    # Drone Control Node - handles MAVROS waypoint services
    drone_control_node = TimerAction(
        period=60.0,  # Wait for all systems to be ready
        actions=[
            Node(
                package='kaskazi_drone',
                executable='drone_control_node',
                name='drone_control_node',
                output='screen',
                parameters=[],
                emulate_tty=True
            )
        ]
    )
    
    # Motion Coordinator - A* path planning action server
    motion_coordinator_node = TimerAction(
        period=65.0,
        actions=[
            Node(
                package='kaskazi_drone',
                executable='motion_coordinator',
                name='motion_coordinator',
                output='screen',
                parameters=[],
                emulate_tty=True
            )
        ]
    )
    
    # Movement Action Client - sends movement goals
    movement_client_node = TimerAction(
        period=75.0,  # Increased wait time for stable system
        actions=[
            Node(
                package='kaskazi_drone',
                executable='movement_action_client',
                name='movement_action_client',
                output='screen',
                parameters=[
                    {'x_param': LaunchConfiguration('x_param')},
                    {'y_param': LaunchConfiguration('y_param')},
                    {'z_param': LaunchConfiguration('z_param')}
                ],
                emulate_tty=True
            )
        ]
    )
    
    return LaunchDescription([
        x_arg,
        y_arg, 
        z_arg,
        px4_sitl,  # Corrected to use px4_sitl instead of ardupilot_sitl
        mavros_node,
        gazebo_bridge,
        drone_control_node,
        motion_coordinator_node,
        movement_client_node
    ])
