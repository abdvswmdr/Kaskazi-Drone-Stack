# Kaskazi Drone Workspace - Development Guidelines

## Project Overview
Professional drone development workspace integrating ArduPilot, Gazebo Harmonic, and ROS2 Jazzy for autonomous drone systems.

**Architecture**: ArduPilot SITL ↔ Native DDS ↔ ROS2 Jazzy ↔ Gazebo Harmonic

## Quick Start Commands

### Workspace Setup
```bash
# Source ROS2 and workspace
source /opt/ros/jazzy/setup.bash
source ~/kaskazi_ws/install/setup.bash

# Build workspace
cd ~/kaskazi_ws
colcon build --symlink-install

# Clean build
colcon build --cmake-clean-cache
```

### ArduPilot Integration
```bash
# Start ArduPilot SITL with DDS
cd ~/ardupilot/ArduCopter
../Tools/autotest/sim_vehicle.py --vehicle=ArduCopter --frame=quad --console --map --dds

# Alternative: Start SITL alone 
../Tools/autotest/sim_vehicle.py -v ArduCopter --console --map
```

### ROS2 DDS Bridge
```bash
# Start Micro-ROS agent (if using DDS)
ros2 run micro_ros_agent micro_ros_agent udp4 --port 2019

# Check DDS topics
ros2 topic list | grep ap
ros2 topic echo /ap/navsat
```

### Gazebo Integration
```bash
# Launch Gazebo with drone model
ros2 launch kaskazi_drone gazebo_iris.launch.py

# Launch complete system (ArduPilot + Gazebo + ROS2)
ros2 launch kaskazi_drone sitl_gazebo.launch.py
```

## Development Workflow

### 1. Testing Sequence
1. **ArduPilot SITL**: Test flight algorithms without physics
2. **SITL + Gazebo**: Add realistic physics simulation  
3. **Full Integration**: Include ROS2 nodes for advanced features

### 2. Vehicle Models
- **Iris**: Default ArduPilot quadcopter (use initially)
- **Crazyflie**: Custom URDF model (add later)

### 3. Build Commands
```bash
# Build specific package
colcon build --packages-select kaskazi_drone

# Build with verbose output
colcon build --event-handlers console_direct+

# Test build
colcon test --packages-select kaskazi_drone
```

## Common Issues & Solutions

### DDS Connection Issues
- Check Micro-ROS agent is running: `ps aux | grep micro_ros_agent`
- Verify DDS parameters in ArduPilot: `param show DDS*`
- Ensure firewall allows UDP port 2019

### Gazebo Plugin Issues  
- Install ArduPilot Gazebo plugin: `sudo apt install ros-jazzy-ardupilot-gazebo`
- Check Gazebo can find models: `echo $GZ_SIM_RESOURCE_PATH`

### Build Failures
- Source setup files before building
- Clear build cache: `rm -rf build/ install/ log/`
- Check dependencies: `rosdep install --from-paths src --ignore-src -r -y`

## Git Workflow

### Branch Strategy
- `main`: Stable integration
- `feature/*`: New features
- `models/*`: Vehicle-specific models (iris, crazyflie)

### Commit Convention
```
type(scope): description

feat(gazebo): add Iris model integration
fix(launch): correct DDS port configuration  
docs(readme): update installation instructions
```

### Pre-commit Checks
```bash
# Format check
ament_clang_format src/
ament_lint_cmake src/

# Test before commit
colcon test
```

## Directory Structure
```
kaskazi_ws/
├── src/kaskazi_drone/
│   ├── launch/          # ROS2 launch files
│   ├── config/          # Parameter files
│   ├── worlds/          # Gazebo world files  
│   ├── urdf/            # Robot descriptions
│   └── kaskazi_drone/   # Python modules
├── build/               # Build artifacts (gitignored)
├── install/             # Install space (gitignored)
└── log/                 # Build logs (gitignored)
```

## Resources
- ArduPilot DDS: [ArduPilot DDS Documentation](https://ardupilot.org/dev/docs/ROS2.html)
- Gazebo Integration: [ArduPilot Gazebo Plugin](https://github.com/ArduPilot/ardupilot_gazebo)
- ROS2 Jazzy: [ROS2 Documentation](https://docs.ros.org/en/jazzy/)

## Notes
- Use native ArduPilot DDS (no MAVROS needed)
- Gazebo Harmonic required for latest features
- Focus on software integration over custom world creation