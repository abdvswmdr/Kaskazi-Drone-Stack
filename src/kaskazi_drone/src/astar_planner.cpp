/**
 * @file astar_planner.cpp
 * @brief Implementation of A* Path Planning Algorithm
 * 
 * This file contains the implementation of the AStar class and Grid3D class
 * for 3D pathfinding with obstacle avoidance.
 */

#include "kaskazi_drone/astar_planner.hpp"
#include <iostream>
#include <cmath>

// Constants for GPS coordinate conversion (rough approximation)
const double LAT_METER = 1.0 / 111000.0;  // Degrees per meter latitude
const double LON_METER = 1.0 / 74000.0;   // Degrees per meter longitude (approximate)

/**
 * @brief Grid3D Constructor
 */
Grid3D::Grid3D(int rows, int cols, int height, const std::vector<GridCoordinate>& obstacles)
  : rows_(rows), cols_(cols), height_(height)
{
  // Initialize 3D grid with all cells as free (false)
  grid_.resize(rows_);
  for (int i = 0; i < rows_; ++i) {
    grid_[i].resize(cols_);
    for (int j = 0; j < cols_; ++j) {
      grid_[i][j].resize(height_, false);
    }
  }
  
  // Mark obstacle cells as occupied (true)
  for (const auto& obstacle : obstacles) {
    if (is_valid(obstacle)) {
      grid_[obstacle.x][obstacle.y][obstacle.z] = true;
    }
  }
}

/**
 * @brief Check if coordinate is valid and not an obstacle
 */
bool Grid3D::is_valid(const GridCoordinate& coord) const
{
  // Check bounds
  if (coord.x < 0 || coord.x >= rows_ ||
      coord.y < 0 || coord.y >= cols_ ||
      coord.z < 0 || coord.z >= height_) {
    return false;
  }
  
  // Check if cell is free (not an obstacle)
  return !grid_[coord.x][coord.y][coord.z];
}

/**
 * @brief Get all valid neighboring coordinates (26-connectivity in 3D)
 */
std::vector<GridCoordinate> Grid3D::get_neighbors(const GridCoordinate& coord) const
{
  std::vector<GridCoordinate> neighbors;
  
  // Check all 26 possible neighbors in 3D space
  for (int dx = -1; dx <= 1; ++dx) {
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dz = -1; dz <= 1; ++dz) {
        // Skip the current cell
        if (dx == 0 && dy == 0 && dz == 0) continue;
        
        GridCoordinate neighbor(coord.x + dx, coord.y + dy, coord.z + dz);
        if (is_valid(neighbor)) {
          neighbors.push_back(neighbor);
        }
      }
    }
  }
  
  return neighbors;
}

/**
 * @brief AStar Constructor
 */
AStar::AStar(std::shared_ptr<Grid3D> grid) : grid_(grid) {}

/**
 * @brief Find path using A* algorithm
 */
std::vector<GridCoordinate> AStar::find_path(const GridCoordinate& start, const GridCoordinate& goal)
{
  // Check if start and goal are valid
  if (!grid_->is_valid(start) || !grid_->is_valid(goal)) {
    std::cout << "Start or goal position is invalid or occupied by obstacle" << std::endl;
    return {};
  }
  
  // Priority queue for open set (nodes to be evaluated)
  std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> open_set;
  
  // Map to store all processed nodes
  std::unordered_map<GridCoordinate, AStarNode, GridCoordinateHash> all_nodes;
  
  // Map to track which nodes are in open set
  std::unordered_map<GridCoordinate, bool, GridCoordinateHash> in_open_set;
  
  // Map to track which nodes are in closed set
  std::unordered_map<GridCoordinate, bool, GridCoordinateHash> in_closed_set;
  
  // Initialize start node
  AStarNode start_node(start);
  start_node.g_cost = 0;
  start_node.h_cost = calculate_heuristic(start, goal);
  start_node.f_cost = start_node.g_cost + start_node.h_cost;
  
  open_set.push(start_node);
  all_nodes[start] = start_node;
  in_open_set[start] = true;
  
  while (!open_set.empty()) {
    // Get node with lowest f_cost
    AStarNode current = open_set.top();
    open_set.pop();
    in_open_set[current.coord] = false;
    
    // Add to closed set
    in_closed_set[current.coord] = true;
    
    // Check if we reached the goal
    if (current.coord == goal) {
      return reconstruct_path(goal, all_nodes);
    }
    
    // Examine all neighbors
    std::vector<GridCoordinate> neighbors = grid_->get_neighbors(current.coord);
    for (const auto& neighbor_coord : neighbors) {
      // Skip if in closed set
      if (in_closed_set[neighbor_coord]) continue;
      
      // Calculate tentative g_cost
      double tentative_g_cost = current.g_cost + calculate_distance(current.coord, neighbor_coord);
      
      // Check if this path to neighbor is better
      bool is_better_path = false;
      if (!in_open_set[neighbor_coord]) {
        // Neighbor not in open set, so this is the first path to it
        is_better_path = true;
      } else if (tentative_g_cost < all_nodes[neighbor_coord].g_cost) {
        // This path is better than the previous one
        is_better_path = true;
      }
      
      if (is_better_path) {
        // Create or update neighbor node
        AStarNode neighbor_node(neighbor_coord);
        neighbor_node.g_cost = tentative_g_cost;
        neighbor_node.h_cost = calculate_heuristic(neighbor_coord, goal);
        neighbor_node.f_cost = neighbor_node.g_cost + neighbor_node.h_cost;
        neighbor_node.parent = current.coord;
        neighbor_node.has_parent = true;
        
        all_nodes[neighbor_coord] = neighbor_node;
        
        if (!in_open_set[neighbor_coord]) {
          open_set.push(neighbor_node);
          in_open_set[neighbor_coord] = true;
        }
      }
    }
  }
  
  // No path found
  std::cout << "No path found from start to goal" << std::endl;
  return {};
}

/**
 * @brief Convert GPS to grid coordinates
 */
GridCoordinate AStar::gps_to_grid(const GPSCoordinate& gps, const GPSCoordinate& home_gps, 
                                 const GridCoordinate& home_grid)
{
  // Calculate offset in meters from home position
  double x_meters = (gps.latitude - home_gps.latitude) / LAT_METER;
  double y_meters = (gps.longitude - home_gps.longitude) / LON_METER;
  double z_meters = gps.altitude - home_gps.altitude;
  
  // Convert to grid coordinates (assuming 1 meter per grid cell)
  int grid_x = home_grid.x + static_cast<int>(std::round(x_meters));
  int grid_y = home_grid.y + static_cast<int>(std::round(y_meters));
  int grid_z = home_grid.z + static_cast<int>(std::round(z_meters));
  
  // Clamp coordinates to grid bounds (assuming grid dimensions are available via grid_)
  // For safety, we'll clamp to reasonable bounds
  grid_x = std::max(0, std::min(grid_x, 14));  // 0-14 for 15x15 grid
  grid_y = std::max(0, std::min(grid_y, 14));  // 0-14 for 15x15 grid  
  grid_z = std::max(0, std::min(grid_z, 14));  // 0-14 for 15x15 grid
  
  return GridCoordinate(grid_x, grid_y, grid_z);
}

/**
 * @brief Convert grid to GPS coordinates
 */
GPSCoordinate AStar::grid_to_gps(const GridCoordinate& grid, const GPSCoordinate& home_gps,
                                const GridCoordinate& home_grid)
{
  // Calculate offset in grid cells from home position
  int x_offset = grid.x - home_grid.x;
  int y_offset = grid.y - home_grid.y;
  int z_offset = grid.z - home_grid.z;
  
  // Convert to GPS coordinates (assuming 1 meter per grid cell)
  double latitude = home_gps.latitude + (x_offset * LAT_METER);
  double longitude = home_gps.longitude + (y_offset * LON_METER);
  double altitude = home_gps.altitude + z_offset;
  
  return GPSCoordinate(latitude, longitude, altitude);
}

/**
 * @brief Calculate 3D Euclidean heuristic distance
 */
double AStar::calculate_heuristic(const GridCoordinate& a, const GridCoordinate& b)
{
  double dx = static_cast<double>(b.x - a.x);
  double dy = static_cast<double>(b.y - a.y);
  double dz = static_cast<double>(b.z - a.z);
  return std::sqrt(dx*dx + dy*dy + dz*dz);
}

/**
 * @brief Calculate actual distance between adjacent coordinates
 */
double AStar::calculate_distance(const GridCoordinate& a, const GridCoordinate& b)
{
  return calculate_heuristic(a, b);  // Same as heuristic for Euclidean distance
}

/**
 * @brief Reconstruct path from goal to start
 */
std::vector<GridCoordinate> AStar::reconstruct_path(const GridCoordinate& goal,
  const std::unordered_map<GridCoordinate, AStarNode, GridCoordinateHash>& nodes)
{
  std::vector<GridCoordinate> path;
  GridCoordinate current = goal;
  
  // Trace back from goal to start using parent pointers
  while (true) {
    path.push_back(current);
    
    auto it = nodes.find(current);
    if (it == nodes.end() || !it->second.has_parent) {
      break;
    }
    
    current = it->second.parent;
  }
  
  // Reverse path to go from start to goal
  std::reverse(path.begin(), path.end());
  return path;
}