#!/usr/bin/env python3
"""
Complete PX4 + XRCE-DDS + A* Path Planning Launch File

Modern PX4 native DDS integration with Gazebo Harmonic and autonomous path planning.
This replaces the MAVROS-based architecture with direct PX4 DDS communication.
"""

import os
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument, ExecuteProcess, TimerAction
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    """Generate complete launch description for PX4 XRCE-DDS A* path planning system."""
    
    # Get package share directory
    pkg_share = FindPackageShare('kaskazi_drone')
    
    # Declare launch arguments for target coordinates
    x_arg = DeclareLaunchArgument(
        'x_param',
        default_value='10.0', 
        description='Target X coordinate for movement (meters)'
    )
    
    y_arg = DeclareLaunchArgument(
        'y_param',
        default_value='10.0', 
        description='Target Y coordinate for movement (meters)'
    )
    
    z_arg = DeclareLaunchArgument(
        'z_param',
        default_value='5.0', 
        description='Target Z coordinate for movement (meters)'
    )

    # Declare model path argument
    model_path_arg = DeclareLaunchArgument(
        'model_path',
        default_value='',
        description='Additional model path for Gazebo'
    )
    
    # XRCE-DDS Agent - MUST start first for PX4 DDS communication
    xrce_dds_agent = Node(
        package='micro_ros_agent',
        executable='micro_ros_agent',
        name='micro_ros_agent',
        arguments=['udp4', '-p', '8888', '-v6'],  # Use UDP on port 8888 with verbose logging
        output='screen',
        emulate_tty=True
    )
    
    # Alternative XRCE-DDS Agent using MicroXRCEAgent binary
    xrce_dds_agent_binary = ExecuteProcess(
        cmd=['MicroXRCEAgent', 'udp4', '-p', '8888'],
        name='xrce_dds_agent_binary',
        output='screen',
        shell=False
    )
    
    # PX4 SITL with Gazebo GUI enabled - Fixed startup configuration
    px4_sitl = TimerAction(
        period=3.0,  # Wait for XRCE-DDS Agent to start
        actions=[
            ExecuteProcess(
                cmd=[
                    'bash', '-c',
                    'cd ~/PX4-Autopilot && '
                    'PX4_SYS_AUTOSTART=4001 PX4_GZ_MODEL=x500 '
                    #   'PX4_GZ_WORLD_FILE=/home/abdvswmdr/kaskazi_ws/src/kaskazi_drone/worlds/empty.sdf make px4_sitl gz_x500'
                    'PX4_GZ_WORLD=walls make px4_sitl gz_x500'
                ],
                name='px4_sitl',
                output='screen',
                shell=False
            )
        ]
    )
    
    # Alternative: Use our enhanced x500 model (commented out for now)
    # px4_sitl_enhanced = TimerAction(
    #     period=3.0,
    #     actions=[
    #         ExecuteProcess(
    #             cmd=[
    #                 'bash', '-c',
    #                 'cd ~/PX4-Autopilot && '
    #                 'HEADLESS=1 PX4_SYS_AUTOSTART=4001 PX4_GZ_MODEL=x500 '
    #                 'PX4_GZ_WORLD=empty make px4_sitl gz_x500'
    #             ],
    #             name='px4_sitl_enhanced',
    #             output='screen',
    #             shell=False
    #         )
    #     ]
    # )
    
    # Motion Coordinator (A* Path Planner) - No changes needed, still works with ROS2 actions
    motion_coordinator = TimerAction(
        period=15.0,  # Wait for PX4 to initialize
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
    
    # PX4 Native Drone Control Node - Replaces MAVROS-based version
    drone_control_node = TimerAction(
        period=20.0,  # Wait for PX4 DDS topics to be available
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
    
    # Movement Action Client - Triggers the A* planning and mission execution
    movement_action_client = TimerAction(
        period=25.0,  # Wait for all systems to be ready
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
    
    # Optional: PX4 topic monitoring node for debugging
    px4_topic_monitor = TimerAction(
        period=30.0,
        actions=[
            ExecuteProcess(
                cmd=['ros2', 'topic', 'list', '|', 'grep', 'fmu'],
                name='px4_topic_monitor',
                output='screen',
                shell=True
            )
        ]
    )

    return LaunchDescription([
        # Launch arguments
        x_arg,
        y_arg,
        z_arg,
        model_path_arg,
        
        # Core system components in order
        xrce_dds_agent_binary,  # Use binary version for reliability
        px4_sitl,  # Use standard x500 model (enhanced model available as alternative)
        motion_coordinator,
        drone_control_node,
        movement_action_client,
        
        # Uncomment for debugging
        # px4_topic_monitor,
    ])
