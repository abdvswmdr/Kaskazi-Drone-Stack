#!/usr/bin/env python3
"""
Launch file for testing the complete drone navigation system.

This launch file starts all three components:
1. Mock MAVROS Service (simulates drone interface)
2. Motion Coordinator (A* planning + action server)
3. Drone Control Node Simple (waypoint interface)
4. Movement Action Client (sends goals)

Usage:
ros2 launch kaskazi_drone test_full_system.launch.py x_param:=2.0 y_param:=3.0 z_param:=5.0
"""

from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument, TimerAction
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    """Generate launch description for full system testing."""
    
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
    
    # 1. Mock MAVROS Service (simulates drone interface)
    mock_mavros_node = Node(
        package='kaskazi_drone',
        executable='mock_mavros_service',
        name='mock_mavros_service',
        output='screen',
        emulate_tty=True
    )
    
    # 2. Motion Coordinator Node (A* planning + action server)
    motion_coordinator_node = TimerAction(
        period=1.0,  # Start after mock MAVROS
        actions=[
            Node(
                package='kaskazi_drone',
                executable='motion_coordinator',
                name='motion_coordinator',
                output='screen',
                emulate_tty=True
            )
        ]
    )
    
    # 3. Drone Control Node Simple (waypoint interface)
    drone_control_node = TimerAction(
        period=2.0,  # Start after motion coordinator
        actions=[
            Node(
                package='kaskazi_drone',
                executable='drone_control_node_simple',
                name='drone_control_node_simple',
                output='screen',
                emulate_tty=True
            )
        ]
    )
    
    # 4. Movement Action Client (sends goals after all services are ready)
    movement_client_node = TimerAction(
        period=4.0,  # Wait for all services to start
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
        mock_mavros_node,
        motion_coordinator_node,
        drone_control_node,
        movement_client_node
    ])