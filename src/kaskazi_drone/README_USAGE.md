# Movement Action Client - PX4 Integration Usage Guide

## Overview
The Movement Action Client is a C++ ROS2 node that sends movement goals (x, y, z coordinates) to the Motion Coordinator, which performs A* path planning and sends waypoints to PX4 via MAVROS.

## Architecture
```
Movement Action Client → Motion Coordinator (A*) → Drone Control Node → MAVROS → PX4
```

## Quick Start

### Option 1: Complete Automated Launch (All-in-One)
```bash
# Start everything automatically (PX4 SITL + MAVROS + A* system)
cd ~/kaskazi_ws
source install/setup.bash
ros2 launch kaskazi_drone px4_astar_complete.launch.py x_param:=10.0 y_param:=10.0 z_param:=15.0
```

### Option 2: Manual Step-by-Step (Recommended for Testing)

#### Step 1: Start PX4 SITL
```bash
# Terminal 1: Start PX4 SITL with Gazebo
cd ~/PX4-Autopilot
make px4_sitl gazebo-classic_iris
```

#### Step 2: Start MAVROS
```bash
# Terminal 2: Start MAVROS with our configuration
cd ~/kaskazi_ws
source install/setup.bash
ros2 run mavros mavros_node --ros-args --params-file src/kaskazi_drone/config/mavros_param.yaml
```

#### Step 3: Arm the Drone
```bash
# Terminal 3: Arm the drone and set to AUTO.MISSION mode
cd ~/kaskazi_ws
source install/setup.bash

# Arm the drone
ros2 service call /mavros/cmd/arming mavros_msgs/srv/CommandBool "{value: true}"

# Set mode to AUTO.MISSION
ros2 service call /mavros/set_mode mavros_msgs/srv/SetMode "{base_mode: 0, custom_mode: 'AUTO.MISSION'}"
```

#### Step 4: Launch A* Path Planning System
```bash
# Terminal 4: Start the complete A* system
cd ~/kaskazi_ws
source install/setup.bash
ros2 launch kaskazi_drone mavros_astar_system.launch.py x_param:=5.0 y_param:=5.0 z_param:=10.0
```

## Movement Action Client Code Explanation

### Key Components (src/movement_action_client.cpp)

1. **Action Client Initialization** (lines 41-43)
   ```cpp
   // Creates an action client to communicate with Motion Coordinator
   this->client_ptr_ = rclcpp_action::create_client<Movement>(
     this, "movement_action_topic");
   ```

2. **Parameter Declaration** (lines 46-48)
   ```cpp
   // Declares ROS2 parameters for target coordinates
   this->declare_parameter<double>("x_param", 0.0);
   this->declare_parameter<double>("y_param", 0.0);
   this->declare_parameter<double>("z_param", 0.0);
   ```

3. **Send Movement Request** (lines 57-92)
   ```cpp
   // Checks if action server is available, creates goal, sends request
   if (!this->client_ptr_->wait_for_action_server(std::chrono::seconds(2))) {
     RCLCPP_ERROR(this->get_logger(), "Action server not available");
     return;
   }
   ```

4. **Callback Functions**
   - **Goal Response** (lines 102-110): Handles server's acceptance/rejection
   - **Feedback** (lines 117-130): Receives continuous progress updates  
   - **Result** (lines 136-157): Handles final success/failure status

## Testing Different Coordinates

### Short Distance Movement
```bash
ros2 launch kaskazi_drone mavros_astar_system.launch.py x_param:=2.0 y_param:=2.0 z_param:=5.0
```

### Long Distance Movement  
```bash
ros2 launch kaskazi_drone mavros_astar_system.launch.py x_param:=20.0 y_param:=15.0 z_param:=25.0
```

### High Altitude Movement
```bash
ros2 launch kaskazi_drone mavros_astar_system.launch.py x_param:=10.0 y_param:=10.0 z_param:=50.0
```

## Monitoring the System

### Check MAVROS Connection
```bash
# Verify MAVROS topics are available
ros2 topic list | grep mavros

# Check drone state
ros2 topic echo /mavros/state

# Check GPS position
ros2 topic echo /mavros/global_position/global
```

### Check Action Communication
```bash
# List available actions
ros2 action list

# Monitor action feedback
ros2 action send_goal /movement_action_topic kaskazi_drone/action/Movement "{x: 5.0, y: 5.0, z: 10.0}"
```

## Troubleshooting

### Common Issues
1. **"Action server not available"**: Motion Coordinator not running
2. **"Waypoint service not available"**: Drone Control Node not running or MAVROS not connected
3. **PX4 connection failed**: Check PX4 SITL is running and UDP ports are correct

### Debug Commands
```bash
# Check if all nodes are running
ros2 node list

# Check service availability
ros2 service list | grep waypoint

# Check action server status
ros2 action list
```

## File Structure
- `src/movement_action_client.cpp`: Main action client implementation
- `include/kaskazi_drone/movement_action_client.hpp`: Header file
- `action/Movement.action`: Action interface definition
- `config/mavros_param.yaml`: MAVROS configuration parameters
- `launch/mavros_astar_system.launch.py`: Complete system launch file