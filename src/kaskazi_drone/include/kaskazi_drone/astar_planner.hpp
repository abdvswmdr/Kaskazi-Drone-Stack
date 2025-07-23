/**
 * @file astar_planner.hpp
 * @brief A* Path Planning Algorithm Implementation
 * 
 * This header defines the AStar class and supporting utilities for 3D path planning
 * in a grid-based environment with obstacle avoidance.
 */

#ifndef KASKAZI_DRONE__ASTAR_PLANNER_HPP_
#define KASKAZI_DRONE__ASTAR_PLANNER_HPP_

#include <vector>
#include <queue>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <memory>

/**
 * @brief Represents a 3D grid coordinate
 */
struct GridCoordinate {
  int x, y, z;
  
  GridCoordinate(int x = 0, int y = 0, int z = 0) : x(x), y(y), z(z) {}
  
  bool operator==(const GridCoordinate& other) const {
    return x == other.x && y == other.y && z == other.z;
  }
  
  bool operator<(const GridCoordinate& other) const {
    if (x != other.x) return x < other.x;
    if (y != other.y) return y < other.y;
    return z < other.z;
  }
};

/**
 * @brief Hash function for GridCoordinate to use in unordered_map
 */
struct GridCoordinateHash {
  std::size_t operator()(const GridCoordinate& coord) const {
    return std::hash<int>()(coord.x) ^ 
           (std::hash<int>()(coord.y) << 1) ^ 
           (std::hash<int>()(coord.z) << 2);
  }
};

/**
 * @brief Represents a 3D GPS coordinate
 */
struct GPSCoordinate {
  double latitude, longitude, altitude;
  
  GPSCoordinate(double lat = 0.0, double lon = 0.0, double alt = 0.0) 
    : latitude(lat), longitude(lon), altitude(alt) {}
};

/**
 * @brief Node in the A* search algorithm
 */
struct AStarNode {
  GridCoordinate coord;
  double g_cost;  // Cost from start to this node
  double h_cost;  // Heuristic cost from this node to goal
  double f_cost;  // Total cost (g + h)
  GridCoordinate parent;
  bool has_parent;
  
  // Default constructor
  AStarNode() : coord(), g_cost(0), h_cost(0), f_cost(0), has_parent(false) {}
  
  AStarNode(const GridCoordinate& coord) 
    : coord(coord), g_cost(0), h_cost(0), f_cost(0), has_parent(false) {}
  
  // Comparison for priority queue (lower f_cost has higher priority)
  bool operator>(const AStarNode& other) const {
    return f_cost > other.f_cost;
  }
};

/**
 * @brief 3D Grid representation for pathfinding
 */
class Grid3D {
public:
  /**
   * @brief Constructor for 3D grid
   * @param rows Number of rows (X dimension)
   * @param cols Number of columns (Y dimension) 
   * @param height Number of height levels (Z dimension)
   * @param obstacles List of obstacle coordinates
   */
  Grid3D(int rows, int cols, int height, const std::vector<GridCoordinate>& obstacles);
  
  /**
   * @brief Check if a coordinate is valid and not an obstacle
   * @param coord Grid coordinate to check
   * @return True if coordinate is valid and free
   */
  bool is_valid(const GridCoordinate& coord) const;
  
  /**
   * @brief Get all valid neighboring coordinates
   * @param coord Current coordinate
   * @return Vector of valid neighboring coordinates
   */
  std::vector<GridCoordinate> get_neighbors(const GridCoordinate& coord) const;
  
  // Getters for grid dimensions
  int get_rows() const { return rows_; }
  int get_cols() const { return cols_; }
  int get_height() const { return height_; }

private:
  int rows_, cols_, height_;
  std::vector<std::vector<std::vector<bool>>> grid_;  // 3D grid: true = obstacle, false = free
};

/**
 * @brief A* Path Planning Algorithm Implementation
 */
class AStar {
public:
  /**
   * @brief Constructor
   * @param grid Pointer to the 3D grid environment
   */
  explicit AStar(std::shared_ptr<Grid3D> grid);
  
  /**
   * @brief Find path from start to goal using A* algorithm
   * @param start Starting grid coordinate
   * @param goal Goal grid coordinate
   * @return Vector of coordinates representing the path (empty if no path found)
   */
  std::vector<GridCoordinate> find_path(const GridCoordinate& start, const GridCoordinate& goal);
  
  /**
   * @brief Convert GPS coordinates to grid coordinates
   * @param gps GPS coordinate to convert
   * @param home_gps Reference GPS coordinate (grid origin)
   * @param home_grid Reference grid coordinate for home position
   * @return Corresponding grid coordinate
   */
  GridCoordinate gps_to_grid(const GPSCoordinate& gps, const GPSCoordinate& home_gps, 
                            const GridCoordinate& home_grid);
  
  /**
   * @brief Convert grid coordinates to GPS coordinates
   * @param grid Grid coordinate to convert
   * @param home_gps Reference GPS coordinate (grid origin)
   * @param home_grid Reference grid coordinate for home position
   * @return Corresponding GPS coordinate
   */
  GPSCoordinate grid_to_gps(const GridCoordinate& grid, const GPSCoordinate& home_gps,
                           const GridCoordinate& home_grid);

private:
  std::shared_ptr<Grid3D> grid_;
  
  /**
   * @brief Calculate heuristic distance between two coordinates (3D Euclidean)
   * @param a First coordinate
   * @param b Second coordinate
   * @return Heuristic distance
   */
  double calculate_heuristic(const GridCoordinate& a, const GridCoordinate& b);
  
  /**
   * @brief Calculate actual distance between two adjacent coordinates
   * @param a First coordinate
   * @param b Second coordinate  
   * @return Actual distance
   */
  double calculate_distance(const GridCoordinate& a, const GridCoordinate& b);
  
  /**
   * @brief Reconstruct path from goal to start using parent pointers
   * @param goal Goal coordinate
   * @param nodes Map of all processed nodes
   * @return Path from start to goal
   */
  std::vector<GridCoordinate> reconstruct_path(const GridCoordinate& goal,
    const std::unordered_map<GridCoordinate, AStarNode, GridCoordinateHash>& nodes);
};

#endif  // KASKAZI_DRONE__ASTAR_PLANNER_HPP_