/*
 * Copyright (c) 2020, Igor Banfi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <costmap_2d/costmap_math.h>
#include <pluginlib/class_list_macros.h>
#include <costmap_tf_layer/costmap_tf_layer.h>
#include <costmap_2d/footprint.h>
#include <costmap_tf_layer/CostmapTfLayerConfig.h>
#include <dynamic_reconfigure/server.h>
#include <Eigen/Dense>

PLUGINLIB_EXPORT_CLASS(costmap_tf_layer::CostmapTfLayer, costmap_2d::Layer)

using costmap_2d::NO_INFORMATION;
using costmap_2d::LETHAL_OBSTACLE;
using costmap_2d::FREE_SPACE;

namespace costmap_tf_layer
{

  CostmapTfLayer::CostmapTfLayer() {}

  CostmapTfLayer::~CostmapTfLayer() {};

  void CostmapTfLayer::onInitialize()
  {
    ros::NodeHandle nh("~/" + name_), g_nh;
    current_ = true;
    default_value_ = NO_INFORMATION;
    matchSize();

    std::string parent_namespace_ = nh.getNamespace();
    parent_namespace_.erase(parent_namespace_.find_last_of('/')+1, parent_namespace_.length());

    ros::NodeHandle costmap_nh_(parent_namespace_);

    global_frame_ = layered_costmap_->getGlobalFrameID();

    dsrv_ = new dynamic_reconfigure::Server<CostmapTfLayerConfig>(nh);
    dynamic_reconfigure::Server<CostmapTfLayerConfig>::CallbackType cb = boost::bind(
        &CostmapTfLayer::reconfigureCB, this, _1, _2);
    dsrv_->setCallback(cb);

    if (!nh.getParam("robot_base_frame", robot_base_frame))
    {
      if (!costmap_nh_.getParam("robot_base_frame", robot_base_frame)) {
        robot_base_frame = "/base_link";
        ROS_INFO("'robot_base_frame' parameter not provided");
        ROS_INFO("'robot_base_frame' set as '%s' ", robot_base_frame.c_str());
      }
      else
        ROS_INFO("'robot_base_frame' set as '%s' ", robot_base_frame.c_str());
    }
    else
      ROS_INFO("'robot_base_frame' set as '%s' ", robot_base_frame.c_str());


    if (!nh.getParam("all_robot_frames", other_robot_frames)) {
      ROS_ERROR("Failed in obtaining param robot_frames");
    }

    robot_footprint = costmap_2d::makeFootprintFromParams(costmap_nh_);
  }


  void CostmapTfLayer::matchSize()
  {
    Costmap2D* master = layered_costmap_->getCostmap();
    resizeMap(master->getSizeInCellsX(), master->getSizeInCellsY(), master->getResolution(),
              master->getOriginX(), master->getOriginY());
  }


  void CostmapTfLayer::reconfigureCB(costmap_tf_layer::CostmapTfLayerConfig &config, uint32_t level)
  {
    enabled_ = config.enabled;

    if (config.footprint != "" && config.footprint != "[]")
   {
     if (costmap_2d::makeFootprintFromString(config.footprint, robot_footprint))
     {
         ROS_INFO("nice");
     }
     else
     {
         ROS_ERROR("Invalid footprint string from dynamic reconfigure");
     }
   }
  }

  void CostmapTfLayer::updateBounds(double robot_x, double robot_y, double robot_yaw, double* min_x,
                                             double* min_y, double* max_x, double* max_y)
  {
    if (layered_costmap_->isRolling())
      updateOrigin(robot_x - getSizeInMetersX() / 2, robot_y - getSizeInMetersY() / 2);
    if (!enabled_)
      return;
    useExtraBounds(min_x, min_y, max_x, max_y);

    bool current = true;

    double wx, wy;
    mapToWorld(0, 0, wx, wy);
    *min_x = std::min(wx, *min_x);
    *min_y = std::min(wy, *min_y);

    mapToWorld(getSizeInMetersX(), getSizeInMetersY(), wx, wy);
    *max_x = std::max(wx, *max_x);
    *max_y = std::max(wy, *max_y);
  }

  void CostmapTfLayer::updateCosts(costmap_2d::Costmap2D& master_grid, int min_i, int min_j, int max_i,
                                            int max_j)
  {
    if (!enabled_)
      return;

    for (std::vector<std::string>::iterator it = other_robot_frames.begin(); it != other_robot_frames.end(); ++it){

      if (robot_base_frame!=*it){
        try {
        //tf::StampedTransform transform;

        geometry_msgs::TransformStamped transform;
        std::string frame_ = *it;

        transform = Layer::tf_->lookupTransform(global_frame_, frame_, ros::Time(0));

        unsigned int mx;
        unsigned int my;

        Eigen::Vector3f robot_position(transform.transform.translation.x, transform.transform.translation.y, 0.0);

        std::vector< geometry_msgs::Point > oriented_robot_footprint;

        costmap_2d::transformFootprint(0.0, 0.0, transform.transform.rotation.z, robot_footprint, oriented_robot_footprint);

        std::vector <base_local_planner::Position2DInt > robot_cells = footprint_helper_.getFootprintCells(robot_position, oriented_robot_footprint, master_grid, true);

        for (int i = 0; i < robot_cells.size(); i++){
          master_grid.setCost(robot_cells[i].x, robot_cells[i].y, LETHAL_OBSTACLE);
        }
        }
        catch (tf::TransformException ex){
          ROS_ERROR("%s",ex.what());
        }
      }
    }
  }
}  // namespace costmap_tf_layer
