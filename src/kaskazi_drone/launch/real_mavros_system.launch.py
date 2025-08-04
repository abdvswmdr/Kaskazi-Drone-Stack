#!/usr/bin/env python3
"""
Real MAVROS Integration Launch File

This launch file integrates the complete drone system with real MAVROS and ArduPilot SITL:
1. ArduPilot SITL with Iris quadcopter
2. Real MAVROS connection (not mock)
3. Gazebo Harmonic simulation
4. Complete A* navigation system with real flight control

Architecture:
Movement Client → Motion Coordinator → Drone Control → Real MAVROS → ArduPilot SITL → Gazebo

Usage:
ros2 launch kaskazi_drone real_mavros_system.launch.py x_param:=5.0 y_param:=10.0 z_param:=15.0
"""

import os
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument, 
    IncludeLaunchDescription, 
    ExecuteProcess, 
    TimerAction,
    OpaqueFunction
)
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, Command
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    """Generate launch description for real MAVROS integration."""
    
    # Get package directories
    pkg_kaskazi_drone = get_package_share_directory('kaskazi_drone')
    
    # Launch arguments for movement goals
    x_arg = DeclareLaunchArgument(
        'x_param',
        default_value='5.0',
        description='Target X coordinate for movement (meters)'
    )
    
    y_arg = DeclareLaunchArgument(
        'y_param', 
        default_value='10.0',
        description='Target Y coordinate for movement (meters)'
    )
    
    z_arg = DeclareLaunchArgument(
        'z_param',
        default_value='15.0', 
        description='Target Z coordinate for movement (meters)'
    )
    
    # ArduPilot SITL arguments
    vehicle_arg = DeclareLaunchArgument(
        'vehicle',
        default_value='ArduCopter',
        description='ArduPilot vehicle type'
    )
    
    frame_arg = DeclareLaunchArgument(
        'frame',
        default_value='quad',
        description='ArduPilot frame type'
    )
    
    use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time',
        default_value='true',
        description='Use simulation time'
    )
    
    # 1. Launch Gazebo with Iris quadcopter (ArduPilot compatible)
    gazebo_iris = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(pkg_kaskazi_drone, 'launch', 'gazebo_iris.launch.py')
        ),
        launch_arguments={
            'use_sim_time': LaunchConfiguration('use_sim_time')
        }.items()
    )
    
    # 2. Start ArduPilot SITL
    ardupilot_sitl = ExecuteProcess(
        cmd=[
            'bash', '-c',
            'cd ~/ardupilot/ArduCopter && ' +
            '../Tools/autotest/sim_vehicle.py ' +
            '--vehicle ArduCopter ' +
            '--frame quad ' +
            '--console --map --no-rebuild'
        ],
        output='screen',
        name='ardupilot_sitl',
        shell=True
    )
    
    # 3. Start Real MAVROS (connects ArduPilot to ROS2)
    real_mavros = TimerAction(
        period=10.0,  # Wait for ArduPilot SITL to start
        actions=[
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource([
                    get_package_share_directory('mavros'), 
                    '/launch/apm.launch'
                ]),
                launch_arguments={
                    'fcu_url': 'udp://:14550@',
                    'gcs_url': '',
                    'target_system_id': '1',
                    'target_component_id': '1',
                    'use_sim_time': LaunchConfiguration('use_sim_time')
                }.items()
            )
        ]
    )
    
    # 4. Motion Coordinator Node (A* planning + action server)
    motion_coordinator_node = TimerAction(
        period=15.0,  # Wait for MAVROS to connect
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
    
    # 5. Real Drone Control Node (MAVROS integration)
    drone_control_node = TimerAction(
        period=16.0,  # Start after motion coordinator
        actions=[
            Node(
                package='kaskazi_drone',
                executable='drone_control_node',  # Use REAL drone control, not simple
                name='drone_control_node',
                output='screen',
                parameters=[{'use_sim_time': LaunchConfiguration('use_sim_time')}],
                emulate_tty=True
            )
        ]
    )
    
    # 6. Movement Action Client (sends goals after all services are ready)
    movement_client_node = TimerAction(
        period=20.0,  # Wait for all services to start
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
    
    return LaunchDescription([
        # Launch arguments
        x_arg,
        y_arg, 
        z_arg,
        vehicle_arg,
        frame_arg,
        use_sim_time_arg,
        
        # System components (in order)
        gazebo_iris,            # Gazebo with Iris quadcopter
        ardupilot_sitl,         # ArduPilot SITL flight controller
        real_mavros,            # Real MAVROS (not mock)
        motion_coordinator_node, # A* planning
        drone_control_node,     # Real drone control (not simple)
        movement_client_node,   # Goal sender
    ])