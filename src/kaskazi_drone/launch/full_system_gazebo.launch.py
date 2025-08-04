#!/usr/bin/env python3
"""
Complete Drone Navigation System with Gazebo Simulation

This launch file integrates:
1. Gazebo simulation with Crazyflie drone
2. A* Motion planning system (all C++ components)
3. Mock MAVROS interface for testing
4. Movement action client for sending goals

Architecture:
Gazebo Crazyflie ← Mock MAVROS ← Drone Control ← Motion Coordinator ← Action Client
                                    ↑              ↑                    ↑
                              Waypoints      A* Planning           User Goals

Usage:
ros2 launch kaskazi_drone full_system_gazebo.launch.py x_param:=2.0 y_param:=3.0 z_param:=4.0
"""

import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, TimerAction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    """Generate launch description for complete system with Gazebo."""
    
    # Get package directory
    pkg_kaskazi_drone = get_package_share_directory('kaskazi_drone')
    
    # Launch arguments for movement goals
    x_arg = DeclareLaunchArgument(
        'x_param',
        default_value='2.0',
        description='Target X coordinate for movement (meters)'
    )
    
    y_arg = DeclareLaunchArgument(
        'y_param', 
        default_value='3.0',
        description='Target Y coordinate for movement (meters)'
    )
    
    z_arg = DeclareLaunchArgument(
        'z_param',
        default_value='4.0', 
        description='Target Z coordinate for movement (meters)'
    )
    
    # Gazebo simulation arguments
    use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time',
        default_value='true',
        description='Use simulation time'
    )
    
    headless_arg = DeclareLaunchArgument(
        'headless',
        default_value='false',
        description='Run Gazebo in headless mode (no GUI)'
    )
    
    # 1. Launch Gazebo with Crazyflie simulation
    gazebo_sim = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(pkg_kaskazi_drone, 'launch', 'crazyflie_gazebo_sim.launch.py')
        ),
        launch_arguments={
            'use_sim_time': LaunchConfiguration('use_sim_time'),
            'x_spawn': '0.0',
            'y_spawn': '0.0', 
            'z_spawn': '0.5'  # Start drone hovering
        }.items()
    )
    
    # 2. Mock MAVROS Service (simulates drone interface)
    mock_mavros_node = TimerAction(
        period=3.0,  # Wait for Gazebo to start
        actions=[
            Node(
                package='kaskazi_drone',
                executable='mock_mavros_service',
                name='mock_mavros_service',
                output='screen',
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}],
                emulate_tty=True
            )
        ]
    )
    
    # 3. Motion Coordinator Node (A* planning + action server)
    motion_coordinator_node = TimerAction(
        period=4.0,  # Start after mock MAVROS
        actions=[
            Node(
                package='kaskazi_drone',
                executable='motion_coordinator',
                name='motion_coordinator',
                output='screen',
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}],
                emulate_tty=True
            )
        ]
    )
    
    # 4. Drone Control Node Simple (waypoint interface)
    drone_control_node = TimerAction(
        period=5.0,  # Start after motion coordinator
        actions=[
            Node(
                package='kaskazi_drone',
                executable='drone_control_node_simple',
                name='drone_control_node_simple',
                output='screen',
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}],
                emulate_tty=True
            )
        ]
    )
    
    # 5. Movement Action Client (sends goals after all services are ready)
    movement_client_node = TimerAction(
        period=7.0,  # Wait for all services to start
        actions=[
            Node(
                package='kaskazi_drone',
                executable='movement_action_client',
                name='movement_action_client',
                output='screen',
                parameters=[
                    {'x_param': LaunchConfiguration('x_param')},
                    {'y_param': LaunchConfiguration('y_param')},
                    {'z_param': LaunchConfiguration('z_param')},
                    {'use_sim_time': LaunchConfiguration('use_sim_time')}
                ],
                emulate_tty=True
            )
        ]
    )
    
    # Optional: RViz for visualization
    rviz_node = TimerAction(
        period=2.0,
        actions=[
            Node(
                package='rviz2',
                executable='rviz2',
                name='rviz2',
                output='screen',
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}],
                arguments=['-d', os.path.join(pkg_kaskazi_drone, 'config', 'crazyflie_sim.rviz')],
                condition=lambda context: LaunchConfiguration('headless').perform(context) == 'false'
            )
        ]
    )
    
    return LaunchDescription([
        # Launch arguments
        x_arg,
        y_arg, 
        z_arg,
        use_sim_time_arg,
        headless_arg,
        
        # System components
        gazebo_sim,              # Gazebo + Crazyflie
        mock_mavros_node,        # Mock drone interface
        motion_coordinator_node, # A* planning
        drone_control_node,      # Waypoint management
        movement_client_node,    # Goal sender
        # rviz_node              # Visualization (commented out for now)
    ])