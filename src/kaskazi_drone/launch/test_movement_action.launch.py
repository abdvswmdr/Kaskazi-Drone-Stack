#!/usr/bin/env python3
"""
Launch file for testing Movement Action Client and Motion Coordinator nodes.

This launch file starts both the Motion Coordinator (action server) and 
Movement Action Client nodes for integration testing.
"""

from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument, ExecuteProcess, TimerAction
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    """Generate launch description for movement action testing."""
    
    # Declare launch arguments for target coordinates
    x_arg = DeclareLaunchArgument(
        'x_param',
        default_value='2.0',
        description='Target X coordinate for movement'
    )
    
    y_arg = DeclareLaunchArgument(
        'y_param', 
        default_value='3.0',
        description='Target Y coordinate for movement'
    )
    
    z_arg = DeclareLaunchArgument(
        'z_param',
        default_value='5.0', 
        description='Target Z coordinate for movement'
    )
    
    # Motion Coordinator node (action server)
    motion_coordinator_node = Node(
        package='kaskazi_drone',
        executable='motion_coordinator',
        name='motion_coordinator',
        output='screen',
        parameters=[],
        emulate_tty=True
    )
    
    # Movement Action Client node (starts after a delay to ensure server is ready)
    movement_client_node = TimerAction(
        period=2.0,  # Wait 2 seconds for server to start
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
        motion_coordinator_node,
        movement_client_node
    ])