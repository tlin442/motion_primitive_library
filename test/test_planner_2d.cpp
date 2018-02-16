#include "timer.hpp"
#include "map_reader.hpp"
#include <motion_primitive_library/collision_checking/voxel_map_util.h>
#include <motion_primitive_library/planner/mp_map_util.h>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>

using namespace MPL;

int main(int argc, char ** argv){
  if(argc != 2) {
    printf(ANSI_COLOR_RED "Input yaml required!\n" ANSI_COLOR_RESET);
    return -1;
  }

  // Load the map 
  MapReader<Vec3i, Vec3f> reader(argv[1]); 
  if(!reader.exist()) {
    printf(ANSI_COLOR_RED "Cannot find input file [%s]!\n" ANSI_COLOR_RESET, argv[1]);
    return -1;
  }

  // Pass the data into a VoxelMapUtil class for collision checking
  std::shared_ptr<VoxelMapUtil> map_util;
  map_util.reset(new VoxelMapUtil);
  map_util->setMap(reader.origin(), reader.dim(), reader.data(), reader.resolution());

  // Initialize planning mission 
  Waypoint3 start, goal;
  start.pos = Vec3f(2.5, -3.5, 0.0); 
  start.vel = Vec3f::Zero(); 
  start.acc = Vec3f::Zero(); 
  start.jrk = Vec3f::Zero(); 
  start.use_pos = true;
  start.use_vel = true;
  start.use_acc = false; 
  start.use_jrk = false; 

  goal.pos = Vec3f(37, 2.5, 0.0);
  goal.vel = Vec3f::Zero(); 
  goal.acc = Vec3f::Zero(); 
  goal.jrk = Vec3f::Zero(); 
 
  goal.use_pos = start.use_pos;
  goal.use_vel = start.use_vel;
  goal.use_acc = start.use_acc;
  goal.use_jrk = start.use_jrk;

  decimal_t u_max = 0.5;
  decimal_t du = u_max;
  vec_Vec3f U;
  for(decimal_t dx = -u_max; dx <= u_max; dx += du )
    for(decimal_t dy = -u_max; dy <= u_max; dy += du )
      U.push_back(Vec3f(dx, dy, 0));

  std::unique_ptr<MPMap3DUtil> planner(new MPMap3DUtil(true)); // Declare a mp planner using voxel map
  planner->setMapUtil(map_util); // Set collision checking function
  planner->setEpsilon(1.0); // Set greedy param (default equal to 1)
  planner->setVmax(1.0); // Set max velocity
  planner->setAmax(1.0); // Set max acceleration 
  planner->setJmax(1.0); // Set max jerk
  planner->setUmax(u_max); // Set max control input
  planner->setDt(1.0); // Set dt for each primitive
  planner->setW(10); // Set dt for each primitive
  planner->setMaxNum(-1); // Set maximum allowed states
  planner->setU(U);// 2D discretization with 1
  planner->setTol(0.2, 0.1, 1); // Tolerance for goal region

  // Planning
  Timer time(true);
  bool valid = planner->plan(start, goal); // Plan from start to goal
  double dt = time.Elapsed().count();
  printf("MP Planner takes: %f ms\n", dt);
  printf("MP Planner expanded states: %zu\n", planner->getCloseSet().size());

  // Plot the result in svg image
  const Vec3i dim = reader.dim();
  typedef boost::geometry::model::d2::point_xy<double> point_2d;
  // Declare a stream and an SVG mapper
  std::ofstream svg("output.svg");
  boost::geometry::svg_mapper<point_2d> mapper(svg, 1000, 1000);

  // Draw the canvas
  boost::geometry::model::polygon<point_2d> bound;
  const double origin_x = reader.origin()(0);
  const double origin_y = reader.origin()(1);
  const double range_x = reader.dim()(0) * reader.resolution();
  const double range_y = reader.dim()(1) * reader.resolution();
  std::vector<point_2d> points;
  points.push_back(point_2d(origin_x, origin_y));
  points.push_back(point_2d(origin_x, origin_y+range_y));
  points.push_back(point_2d(origin_x+range_x, origin_y+range_y));
  points.push_back(point_2d(origin_x+range_x, origin_y));
  points.push_back(point_2d(origin_x, origin_y));
  boost::geometry::assign_points(bound, points);
  boost::geometry::correct(bound);

  mapper.add(bound);
  mapper.map(bound, "fill-opacity:1.0;fill:rgb(255,255,255);stroke:rgb(0,0,0);stroke-width:2"); // White

  // Draw start and goal
  point_2d start_pt, goal_pt;
  boost::geometry::assign_values(start_pt, start.pos(0), start.pos(1));
  mapper.add(start_pt);
  mapper.map(start_pt, "fill-opacity:1.0;fill:rgb(255,0,0);", 10); // Red
  boost::geometry::assign_values(goal_pt, goal.pos(0), goal.pos(1));
  mapper.add(goal_pt);
  mapper.map(goal_pt, "fill-opacity:1.0;fill:rgb(255,0,0);", 10); // Red



  // Draw the obstacles
  for(int x = 0; x < dim(0); x ++) {
    for(int y = 0; y < dim(1); y ++) {
      if(!map_util->isFree(Vec3i(x, y, 0))) {
        Vec3f pt = map_util->intToFloat(Vec3i(x, y, 0));
        point_2d a;
        boost::geometry::assign_values(a, pt(0), pt(1));
        mapper.add(a);
        mapper.map(a, "fill-opacity:1.0;fill:rgb(0,0,0);", 1);
      }
    }
  }

  // Draw expended states
  for(const auto& pt: planner->getCloseSet()) {
    point_2d a;
    boost::geometry::assign_values(a, pt(0), pt(1));
    mapper.add(a);
    mapper.map(a, "fill-opacity:1.0;fill:rgb(100,100,200);", 2); // Blue
  }


  // Draw the trajectory
  if(valid) {
    Trajectory3 traj = planner->getTraj();
    double total_t = traj.getTotalTime();
    printf("Total time T: %f\n", total_t);
    printf("Total J:  J(0) = %f, J(1) = %f, J(2) = %f, J(3) = %f\n", 
        traj.J(0), traj.J(1), traj.J(2), traj.J(3));
    int num = 100; // number of points on trajectory to draw
    const double dt = total_t / num; 
    boost::geometry::model::linestring<point_2d> line;
    for(double t = 0; t <= total_t; t += dt) {
      Waypoint3 w;
      traj.evaluate(t, w);
      line.push_back(point_2d(w.pos(0), w.pos(1)));
    }
    mapper.add(line);
    mapper.map(line, "opacity:0.4;fill:none;stroke:rgb(212,0,0);stroke-width:5"); // Red
  }
 

  return 0;
}
