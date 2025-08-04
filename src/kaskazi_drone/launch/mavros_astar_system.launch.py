#!/usr/bin/env python3
"""
MAVROS + A* Path Planning System Launch File

This launch file assumes PX4 SITL and MAVROS are already running.
It starts the drone control pipeline:
1. Drone Control Node (MAVROS waypoint interface)
2. Motion Coordinator (A* path planning action server)  
3. Movement Action Client (sends movement goals)

Prerequisites:
- PX4 SITL running: cd ~/PX4-Autopilot && make px4_sitl gazebo-classic_iris
- MAVROS running: ros2 run mavros mavros_node --ros-args --params-file ~/kaskazi_ws/src/kaskazi_drone/config/mavros_param.yaml

Usage:
ros2 launch kaskazi_drone mavros_astar_system.launch.py x_param:=5.0 y_param:=5.0 z_param:=10.0
"""

from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument, TimerAction
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    """Generate launch description for MAVROS-based A* path planning system."""
    
    # Declare launch arguments for target coordinates
    x_arg = DeclareLaunchArgument(
        'x_param',
        default_value='5.0',
        description='Target X coordinate for movement (meters)'
    )
    
    y_arg = DeclareLaunchArgument(
        'y_param', 
        default_value='5.0',
        description='Target Y coordinate for movement (meters)'
    )
    
    z_arg = DeclareLaunchArgument(
        'z_param',
        default_value='10.0', 
        description='Target Z coordinate for movement (meters)'
    )
    
    # Drone Control Node - handles MAVROS waypoint services
    drone_control_node = Node(
        package='kaskazi_drone',
        executable='drone_control_node',
        name='drone_control_node',
        output='screen',
        parameters=[],
        emulate_tty=True
    )
    
    # Motion Coordinator - A* path planning action server
    motion_coordinator_node = TimerAction(
        period=2.0,  # Wait 2 seconds for drone control to be ready
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
        period=4.0,  # Wait 4 seconds for motion coordinator to be ready
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
        drone_control_node,
        motion_coordinator_node,
        movement_client_node
    ])