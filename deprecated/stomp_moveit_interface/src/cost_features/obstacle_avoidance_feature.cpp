/*
 * obstacle_avoidance_feature.cpp
 *
 *  Created on: Oct 19, 2015
 *      Author: rosindustrial
 */

#include <stomp_moveit_interface/obstacle_avoidance_feature.h>

PLUGINLIB_EXPORT_CLASS(stomp_moveit_interface::ObstacleAvoidanceFeature,stomp_moveit_interface::StompCostFeature)

const int NUM_FEATURE_VALUES = 1;
const double DEFAULT_CLEARANCE = 0.01f;
const std::string DEFAULT_COLLISION_DETECTOR = "IndustrialFCL";
const std::string FEATURE_NAME = "ObstacleAvoidance";

namespace stomp_moveit_interface
{

ObstacleAvoidanceFeature::ObstacleAvoidanceFeature():
    clearance_(DEFAULT_CLEARANCE)
{

}

ObstacleAvoidanceFeature::~ObstacleAvoidanceFeature()
{
  // TODO Auto-generated destructor stub
}

bool ObstacleAvoidanceFeature::initialize(XmlRpc::XmlRpcValue& config,
                                          int num_threads,
                                          const std::string& group_name,
                                          planning_scene::PlanningSceneConstPtr planning_scene)
{

  return StompCostFeature::initialize(config,
                                      num_threads,
                                      group_name,
                                      planning_scene) && loadParameters(config);

}

void ObstacleAvoidanceFeature::setPlanningScene(planning_scene::PlanningSceneConstPtr planning_scene)
{
  StompCostFeature::setPlanningScene(planning_scene);

  //Check and make sure the correct collision detector is loaded.
  if (planning_scene->getActiveCollisionDetectorName() != DEFAULT_COLLISION_DETECTOR)
  {
    throw std::runtime_error("STOMP Moveit Interface requires the use of collision detector \"" + DEFAULT_COLLISION_DETECTOR + "\"\n"
                             "To resolve the issue add the ros parameter collision_detector = " + DEFAULT_COLLISION_DETECTOR +
                             ".\nIt is recommend to added it where the move_group node is launched, usually in the in the "
                             "(robot_name)_moveit_config/launch/move_group.launch");
  }
  collision_robot_ = boost::dynamic_pointer_cast<const collision_detection::CollisionRobotIndustrial>(planning_scene->getCollisionRobot());
  collision_world_ = boost::dynamic_pointer_cast<const collision_detection::CollisionWorldIndustrial>(planning_scene->getCollisionWorld());
}

bool ObstacleAvoidanceFeature::loadParameters(XmlRpc::XmlRpcValue& config)
{
  // initialize collision request
  collision_request_.group_name = group_name_;
  collision_request_.cost = false;
  collision_request_.distance = true;
  collision_request_.max_contacts = 1;
  collision_request_.max_contacts_per_pair = 1;
  collision_request_.contacts = true;
  collision_request_.verbose = false;

  if(config.hasMember("collision_clearance"))
  {
    clearance_ = static_cast<double>(config["collision_clearance"]);
    if(clearance_ == 0.0)
    {
      clearance_ = DEFAULT_CLEARANCE;
      ROS_WARN_STREAM("Clearance can not be 0, using default "<<clearance_);
    }
  }
  else
  {
    ROS_ERROR_STREAM(getName()<<" feature failed to load parameters");
    return false;
  }

  ROS_DEBUG_STREAM("Obstacle Avoidance feature initialized");

  return true;
}

int ObstacleAvoidanceFeature::getNumValues() const
{
  return NUM_FEATURE_VALUES;
}

void ObstacleAvoidanceFeature::computeValuesAndGradients(const boost::shared_ptr<StompTrajectory const>& trajectory,
                                       Eigen::MatrixXd& feature_values,         // num_time_steps x num_features
                                       bool compute_gradients,
                                       std::vector<Eigen::MatrixXd>& gradients, // [num_features] num_joints x num_time_steps
                                       std::vector<int>& validities,             // [num_time_steps] each state valid or not
                                       int thread_id,
                                       int start_timestep,                      // start timestep
                                       int num_time_steps) const
{
  typedef collision_detection::CollisionResult::ContactMap ContactMap;
  typedef ContactMap::iterator ContactMapIterator;
  typedef std::vector<collision_detection::Contact> ContactArray;

  // initializing result arrays (gradients are not used by stomp)
  feature_values = Eigen::MatrixXd::Zero(trajectory->num_time_steps_, getNumValues());
  validities.assign(trajectory->num_time_steps_, 1);

  collision_detection::CollisionRequest request = collision_request_;
  request.group_name = trajectory->group_name_;
  collision_detection::CollisionResult result_world_collision, result_robot_collision;
  std::vector<collision_detection::CollisionResult> results(2);
  moveit::core::RobotStatePtr state0(new moveit::core::RobotState(planning_scene_->getRobotModel()));
  double max_depth = 0;
  for (int t=start_timestep; t<start_timestep + num_time_steps; ++t)
  {
    *state0 = trajectory->kinematic_states_[t] ;
    state0->update();

    // checking robot vs world (attached objects, octomap, not in urdf) collisions
    result_world_collision.distance = std::numeric_limits<double>::max();

    collision_world_->checkRobotCollision(request,
                                          result_world_collision,
                                          *collision_robot_,
                                          *state0,
                                          planning_scene_->getAllowedCollisionMatrix());

    collision_robot_->checkSelfCollision(request,
                                         result_robot_collision,
                                         *state0,
                                         planning_scene_->getAllowedCollisionMatrix());

    results[0]= result_world_collision;
    results[1] = result_robot_collision;
    for(std::vector<collision_detection::CollisionResult>::iterator i = results.begin(); i != results.end(); i++)
    {
      collision_detection::CollisionResult& result = *i;
      double potential = 0.0;
      double depth = 0;
      if(result.collision)
      {
        for(ContactMapIterator c = result.contacts.begin(); c != result.contacts.end(); c++)
        {
          ContactArray& contacts = c->second;
          for(ContactArray::iterator ci = contacts.begin(); ci != contacts.end() ; ci++)
          {
            collision_detection::Contact& contact = *ci;
            depth = std::abs(contact.depth) ;
            max_depth = depth > max_depth ? depth : max_depth;

          }
        }

        potential = depth + clearance_;
        validities[t] = 0;
      }
      else
      {
        if( (result.distance > 0) )
        {
          if(result.distance < clearance_)
          {
            potential = (clearance_- result.distance);
          }
        }

      }

      feature_values(t,0) += potential;
    }
  }

  feature_values*=(1/(max_depth + clearance_));
}

std::string ObstacleAvoidanceFeature::getName() const
{
  return FEATURE_NAME;
}

void ObstacleAvoidanceFeature::getNames(std::vector<std::string>& names) const
{
  names.push_back(FEATURE_NAME);
}

} /* namespace stomp_moveit_interface */
