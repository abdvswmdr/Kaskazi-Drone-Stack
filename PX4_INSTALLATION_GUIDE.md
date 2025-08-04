# PX4 XRCE-DDS Integration Setup Guide

## Complete Installation Guide for PX4 Native DDS Communication with ROS2 Jazzy on Ubuntu 24.04

This guide provides step-by-step instructions for setting up PX4 autopilot with native XRCE-DDS communication, Gazebo Harmonic simulation, and ROS2 Jazzy integration on Ubuntu 24.04. This setup enables direct communication between PX4 and ROS2 without MAVROS middleware.

---

## Table of Contents

1. [System Requirements](#system-requirements)
2. [Environment Setup](#environment-setup)
3. [PX4 Autopilot Installation](#px4-autopilot-installation)
4. [ROS2 PX4 Integration Packages](#ros2-px4-integration-packages)
5. [Micro XRCE-DDS Agent Installation](#micro-xrce-dds-agent-installation)
6. [Gazebo Harmonic Integration](#gazebo-harmonic-integration)
7. [Testing the Installation](#testing-the-installation)
8. [Launch System Configuration](#launch-system-configuration)
9. [Troubleshooting](#troubleshooting)
10. [References](#references)

---

## System Requirements

- **Operating System**: Ubuntu 24.04 LTS (Noble Numbat)
- **ROS2 Distribution**: Jazzy Jalisco
- **Gazebo Version**: Harmonic
- **Python Version**: 3.12+
- **Disk Space**: 15GB+ free space
- **Memory**: 8GB+ RAM recommended

---

## Environment Setup

### 1. Update System Packages

```bash
sudo apt update && sudo apt upgrade -y
```

### 2. Install Essential Development Tools

```bash
sudo apt install -y \
    build-essential \
    cmake \
    git \
    python3-pip \
    python3-dev \
    python3-setuptools \
    python3-wheel \
    curl \
    wget \
    gnupg \
    lsb-release
```

### 3. Install ROS2 Jazzy (if not already installed)

```bash
# Add ROS2 GPG key
sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o /usr/share/keyrings/ros-archive-keyring.gpg

# Add ROS2 repository
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null

# Update package list and install ROS2 Jazzy
sudo apt update
sudo apt install -y ros-jazzy-desktop

# Install additional ROS2 tools
sudo apt install -y \
    python3-rosdep \
    python3-rosinstall \
    python3-rosinstall-generator \
    python3-wstool \
    python3-colcon-common-extensions \
    ros-jazzy-gazebo-ros-pkgs
```

### 4. Initialize rosdep

```bash
sudo rosdep init
rosdep update
```

---

## PX4 Autopilot Installation

### 1. Clone PX4 Autopilot Repository

```bash
cd ~
git clone https://github.com/PX4/PX4-Autopilot.git --recursive
cd PX4-Autopilot
```

### 2. Install PX4 Dependencies

```bash
# Run PX4 dependency installation script
bash ./Tools/setup/ubuntu.sh

# Install additional Python dependencies
pip3 install --user -r Tools/setup/requirements.txt
```

### 3. Install Gazebo Harmonic

```bash
# Add Gazebo GPG key
sudo wget https://packages.osrfoundation.org/gazebo.gpg -O /usr/share/keyrings/pkgs-osrf-archive-keyring.gpg

# Add Gazebo repository
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/pkgs-osrf-archive-keyring.gpg] http://packages.osrfoundation.org/gazebo/ubuntu-stable $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/gazebo-stable.list > /dev/null

# Update and install Gazebo Harmonic
sudo apt update
sudo apt install -y gz-harmonic
```

### 4. Build PX4 with Gazebo Support

```bash
cd ~/PX4-Autopilot
make px4_sitl gz_x500
```

**Note**: The first build may take 20-30 minutes depending on your system.

---

## ROS2 PX4 Integration Packages

### 1. Create ROS2 Workspace

```bash
mkdir -p ~/kaskazi_ws/src
cd ~/kaskazi_ws/src
```

### 2. Clone PX4 ROS2 Integration Packages

```bash
# Clone px4_msgs - PX4 message definitions for ROS2
git clone https://github.com/PX4/px4_msgs.git

# Clone px4_ros_com - PX4-ROS2 communication bridge
git clone https://github.com/PX4/px4_ros_com.git
```

### 3. Install ROS2 Dependencies

```bash
cd ~/kaskazi_ws
rosdep install --from-paths src --ignore-src -r -y
```

### 4. Build ROS2 Packages

```bash
# Source ROS2 environment
source /opt/ros/jazzy/setup.bash

# Build the workspace
colcon build --packages-select px4_msgs
colcon build --packages-select px4_ros_com

# Source the workspace
source install/setup.bash
```

---

## Micro XRCE-DDS Agent Installation

The Micro XRCE-DDS Agent enables lightweight DDS communication between PX4 and ROS2.

### 1. Install Dependencies

```bash
sudo apt install -y \
    libasio-dev \
    libtinyxml2-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libfastcdr-dev \
    libfastrtps-dev
```

### 2. Clone and Build Micro XRCE-DDS Agent

```bash
cd ~/kaskazi_ws/src
git clone https://github.com/eProsima/Micro-XRCE-DDS-Agent.git
cd Micro-XRCE-DDS-Agent
mkdir build && cd build

# Configure build
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DUAGENT_SOCKETCAN_PROFILE=OFF \
    -DUAGENT_P2P_PROFILE=OFF

# Build and install
make -j$(nproc)
sudo make install

# Update library cache
sudo ldconfig
```

### 3. Verify Installation

```bash
# Check if MicroXRCEAgent is available
which MicroXRCEAgent

# Test agent startup
MicroXRCEAgent udp4 -p 8888 &
sleep 2
killall MicroXRCEAgent
```

---

## Gazebo Harmonic Integration

### 1. Install ArduPilot Gazebo Plugin

```bash
sudo apt install -y ros-jazzy-ardupilot-gazebo
```

### 2. Configure Environment Variables

Add the following to your `~/.bashrc`:

```bash
# PX4 and Gazebo environment setup
export PX4_HOME=$HOME/PX4-Autopilot
export GZ_SIM_RESOURCE_PATH=$GZ_SIM_RESOURCE_PATH:$PX4_HOME/Tools/simulation/gz/worlds:$PX4_HOME/Tools/simulation/gz/models

# ROS2 environment
source /opt/ros/jazzy/setup.bash
source ~/kaskazi_ws/install/setup.bash

# Gazebo environment
export GZ_VERSION=harmonic
```

### 3. Reload Environment

```bash
source ~/.bashrc
```

---

## Testing the Installation

### 1. Test PX4 SITL with Gazebo

Open a new terminal and run:

```bash
cd ~/PX4-Autopilot
make px4_sitl gz_x500
```

You should see:
- PX4 SITL startup messages
- Gazebo Harmonic launching with x500 quadrotor
- Vehicle appearing in Gazebo simulation

### 2. Test XRCE-DDS Agent Connection

In a new terminal:

```bash
# Start XRCE-DDS Agent
MicroXRCEAgent udp4 -p 8888
```

In another terminal:

```bash
# Check PX4 topics are published to ROS2
source ~/kaskazi_ws/install/setup.bash
ros2 topic list | grep fmu

# You should see topics like:
# /fmu/in/vehicle_command
# /fmu/out/vehicle_status
# /fmu/out/vehicle_local_position
```

### 3. Test Topic Communication

```bash
# Echo vehicle status
ros2 topic echo /fmu/out/vehicle_status_v1

# Publish a test command (arm the vehicle)
ros2 topic pub --once /fmu/in/vehicle_command px4_msgs/msg/VehicleCommand '{
  timestamp: 0,
  command: 400,
  param1: 1.0,
  target_system: 1,
  target_component: 1,
  source_system: 1,
  source_component: 1,
  from_external: true
}'
```

---

## Launch System Configuration

### 1. Create Launch File

Create a launch file for integrated system startup:

```python
# ~/kaskazi_ws/src/your_package/launch/px4_complete.launch.py

from launch import LaunchDescription
from launch.actions import ExecuteProcess, TimerAction
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        # Start XRCE-DDS Agent
        ExecuteProcess(
            cmd=['MicroXRCEAgent', 'udp4', '-p', '8888'],
            name='xrce_dds_agent',
            output='screen'
        ),
        
        # Start PX4 SITL with delay
        TimerAction(
            period=3.0,
            actions=[ExecuteProcess(
                cmd=[
                    'bash', '-c',
                    'cd ~/PX4-Autopilot && '
                    'PX4_SYS_AUTOSTART=4001 PX4_GZ_MODEL=x500 '
                    'make px4_sitl gz_x500'
                ],
                name='px4_sitl',
                output='screen'
            )]
        ),
        
        # Your ROS2 nodes can be added here
    ])
```

### 2. Test Complete System

```bash
cd ~/kaskazi_ws
colcon build
source install/setup.bash
ros2 launch your_package px4_complete.launch.py
```

---

## Troubleshooting

### Common Issues and Solutions

#### 1. XRCE-DDS Agent Not Found

**Problem**: `MicroXRCEAgent: command not found`

**Solution**:
```bash
# Verify installation path
which MicroXRCEAgent

# If not found, add to PATH
echo 'export PATH=$PATH:/usr/local/bin' >> ~/.bashrc
source ~/.bashrc

# Or reinstall with different prefix
cd ~/kaskazi_ws/src/Micro-XRCE-DDS-Agent/build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
make -j$(nproc)
make install
echo 'export PATH=$PATH:$HOME/.local/bin' >> ~/.bashrc
source ~/.bashrc
```

#### 2. Library Loading Issues

**Problem**: `libmicroxrcedds_agent.so.3.0: cannot open shared object file`

**Solution**:
```bash
# Create library configuration
echo '/usr/local/lib' | sudo tee /etc/ld.so.conf.d/microxrce.conf
sudo ldconfig

# Verify library is found
ldconfig -p | grep microxrce
```

#### 3. Gazebo Model Loading Issues

**Problem**: Models not appearing in Gazebo

**Solution**:
```bash
# Verify environment variables
echo $GZ_SIM_RESOURCE_PATH

# Add PX4 model paths
export GZ_SIM_RESOURCE_PATH=$GZ_SIM_RESOURCE_PATH:$PX4_HOME/Tools/simulation/gz/worlds:$PX4_HOME/Tools/simulation/gz/models

# Test model discovery
gz model --list
```

#### 4. ROS2 Topic Communication Issues

**Problem**: No topics visible or communication failing

**Solution**:
```bash
# Check ROS2 domain
echo $ROS_DOMAIN_ID

# Verify XRCE-DDS Agent is running
ps aux | grep MicroXRCEAgent

# Check PX4 DDS configuration
# In PX4 console:
param show UXRCE*
```

#### 5. Build Errors

**Problem**: Compilation failures during build

**Solution**:
```bash
# Clean build directories
cd ~/kaskazi_ws
rm -rf build/ install/ log/

# Update dependencies
rosdep update
rosdep install --from-paths src --ignore-src -r -y

# Build with verbose output
colcon build --event-handlers console_direct+
```

---

## Performance Optimization

### 1. System Configuration

```bash
# Increase UDP buffer sizes for better DDS performance
echo 'net.core.rmem_max = 2147483647' | sudo tee -a /etc/sysctl.conf
echo 'net.core.rmem_default = 2147483647' | sudo tee -a /etc/sysctl.conf
echo 'net.core.wmem_max = 2147483647' | sudo tee -a /etc/sysctl.conf
echo 'net.core.wmem_default = 2147483647' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

### 2. PX4 Configuration

Add to PX4 startup script or set via QGroundControl:

```bash
# Enable XRCE-DDS
param set UXRCE_DDS_CFG 102  # UDP port 8888
param set SYS_AUTOSTART 4001 # x500 quadrotor
param set SYS_MC_EST_GROUP 2 # Use EKF2
```

---

## References

- [PX4 User Guide](https://docs.px4.io/main/en/)
- [PX4 ROS2 Interface](https://docs.px4.io/main/en/ros/ros2_comm.html)
- [Micro XRCE-DDS Agent Documentation](https://micro-xrce-dds.docs.eprosima.com/en/latest/agent.html)
- [ROS2 Jazzy Documentation](https://docs.ros.org/en/jazzy/)
- [Gazebo Harmonic Documentation](https://gazebosim.org/docs/harmonic/overview)
- [Fast DDS Documentation](https://fast-dds.docs.eprosima.com/en/latest/)

## Version Information

- **Guide Version**: 1.0
- **Last Updated**: January 2025
- **Tested On**: Ubuntu 24.04 LTS with ROS2 Jazzy
- **PX4 Version**: 1.14+
- **Gazebo Version**: Harmonic

---

## Contributing

Found an issue or have improvements? Please contribute back to the project repository or create an issue for documentation updates.

## License

This guide is provided as-is for educational and development purposes. Follow the respective licenses of PX4, ROS2, and other referenced projects.