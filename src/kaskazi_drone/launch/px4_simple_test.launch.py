#!/usr/bin/env python3
"""
Simple PX4 XRCE-DDS Test Launch File

Minimal launch file to test PX4 DDS integration step by step.
"""

import os
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument, ExecuteProcess, TimerAction
from launch.substitutions import LaunchConfiguration

def generate_launch_description():
    """Generate simple test launch description."""
    
    # Step 1: Start XRCE-DDS Agent using binary
    xrce_dds_agent = ExecuteProcess(
        cmd=['MicroXRCEAgent', 'udp4', '-p', '8888'],
        name='xrce_dds_agent',
        output='screen',
        shell=False
    )
    
    # Step 2: Start PX4 SITL (delayed to let agent start)
    px4_sitl = TimerAction(
        period=5.0,
        actions=[
            ExecuteProcess(
                cmd=[
                    'bash', '-c',
                    'cd ~/PX4-Autopilot && '
                    'HEADLESS=1 PX4_SYS_AUTOSTART=4001 PX4_GZ_WORLD=default make px4_sitl gz_x500'
                ],
                name='px4_sitl',
                output='screen',
                shell=False
            )
        ]
    )

    return LaunchDescription([
        xrce_dds_agent,
        px4_sitl,
    ])