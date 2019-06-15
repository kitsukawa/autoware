/*
 * Copyright 2015-2019 Autoware Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "nmea2tfpose_core.h"

namespace gnss_localizer
{
// Constructor
Nmea2TFPoseNode::Nmea2TFPoseNode()
  : private_nh_("~")
  , MAP_FRAME_("map")
  , GPS_FRAME_("gps")
  , roll_(0)
  , pitch_(0)
  , yaw_(0)
  , orientation_time_(0)
  , position_time_(0)
  , current_time_(0)
  , orientation_stamp_(0)
{
  initForROS();
  geo_.set_plane(plane_number_);
}

// Destructor
Nmea2TFPoseNode::~Nmea2TFPoseNode()
{
}

void Nmea2TFPoseNode::initForROS()
{
  // ros parameter settings
  private_nh_.getParam("plane", plane_number_);
  private_nh_.getParam("rosbag", rosbag_);
  // setup subscriber
  sub1_ = nh_.subscribe("nmea_sentence", 100, &Nmea2TFPoseNode::callbackFromNmeaSentence, this);

  // setup publisher
  pub1_ = nh_.advertise<geometry_msgs::PoseStamped>("gnss_pose", 10);
}

void Nmea2TFPoseNode::run()
{
  char buffer[80];
  std::time_t now = std::time(NULL);
  std::tm* pnow = std::localtime(&now);
  std::strftime(buffer, 80, "%Y%m%d_%H%M%S", pnow);
  std::string directory_name = "/tmp/Autoware/log/nmea2tfpose";
  std::string filename = directory_name + "/" + std::string(buffer) + ".csv";
  boost::filesystem::create_directories(boost::filesystem::path(directory_name));
  ofs.open(filename.c_str(), std::ios::app);
  ofs << "msg->header.stamp" << ","
      << "x_" << ","
      << "y_" << ","
      << "z_" << ","
      << "roll_" << ","
      << "pitch_" << ","
      << "yaw_" << ","
      << "quality_" << ","
      << "num_satellite_" << ","
      << "lat" << ","
      << "lon" << ","
      << "alt" << ","
      << "lat2" << ","
      << "lon2" << ","
      << "alt2"
      << std::endl;

  rosbag::Bag bag;
  std::cout << "rosbag: " << rosbag_ << std::endl;
  bag.open(rosbag_, rosbag::bagmode::Read);

  rosbag::View view(bag, rosbag::TopicQuery("/nmea_sentence"));

  int msg_num = view.size();
  std::cout << "msg_num: " << msg_num << std::endl;
  int num = 1;
  foreach(rosbag::MessageInstance const m, view){
    nmea_msgs::Sentence::ConstPtr nmea_msg = m.instantiate<nmea_msgs::Sentence>();
    if(nmea_msg != NULL){
      Nmea2TFPoseNode::callbackFromNmeaSentence(nmea_msg);
      std::cout << num << " / " << msg_num << std::endl;
      num++;
    }
  }

//  ros::spin();
}

void Nmea2TFPoseNode::publishPoseStamped()
{
  geometry_msgs::PoseStamped pose;
  pose.header.frame_id = MAP_FRAME_;
  pose.header.stamp = current_time_;
  pose.pose.position.x = geo_.y();
  pose.pose.position.y = geo_.x();
  pose.pose.position.z = geo_.z();
  x_ = pose.pose.position.x;
  y_ = pose.pose.position.y;
  z_ = pose.pose.position.z;
  pose.pose.orientation = tf::createQuaternionMsgFromRollPitchYaw(roll_, pitch_, yaw_);
  pub1_.publish(pose);
}

void Nmea2TFPoseNode::publishTF()
{
  tf::Transform transform;
  transform.setOrigin(tf::Vector3(geo_.y(), geo_.x(), geo_.z()));
  tf::Quaternion quaternion;
  quaternion.setRPY(roll_, pitch_, yaw_);
  transform.setRotation(quaternion);
  br_.sendTransform(tf::StampedTransform(transform, current_time_, MAP_FRAME_, GPS_FRAME_));
}

void Nmea2TFPoseNode::createOrientation()
{
  yaw_ = atan2(geo_.x() - last_geo_.x(), geo_.y() - last_geo_.y());
  roll_ = 0;
  pitch_ = 0;
}

void Nmea2TFPoseNode::convert(std::vector<std::string> nmea, ros::Time current_stamp)
{
  try
  {
    if (nmea.at(0).compare(0, 2, "QQ") == 0)
    {
      orientation_time_ = stod(nmea.at(3));
      roll_ = stod(nmea.at(4)) * M_PI / 180.;
      pitch_ = -1 * stod(nmea.at(5)) * M_PI / 180.;
      yaw_ = -1 * stod(nmea.at(6)) * M_PI / 180. + M_PI / 2;
      orientation_stamp_ = current_stamp;
      ROS_INFO("QQ is subscribed.");
    }
    else if (nmea.at(0) == "$PASHR")
    {
      orientation_time_ = stod(nmea.at(1));
      roll_ = stod(nmea.at(4)) * M_PI / 180.;
      pitch_ = -1 * stod(nmea.at(5)) * M_PI / 180.;
      yaw_ = -1 * stod(nmea.at(2)) * M_PI / 180. + M_PI / 2;
      ROS_INFO("PASHR is subscribed.");
    }
    else if(nmea.at(0).compare(3, 3, "GGA") == 0)
    {
      position_time_ = stod(nmea.at(1));
      double lat = stod(nmea.at(2));
      double lon = stod(nmea.at(4));

      double lat1, lad1, lod1, lon1;
      // 1234.56 -> 12'34.56 -> 12+ 34.56/60
      lad1 = floor(lat / 100.);
      lat1 = lat - lad1 * 100.;
      lod1 = floor(lon / 100.);
      lon1 = lon - lod1 * 100.;

      // Changing Longitude and Latitude to Radians
      lat_ = (lad1 + lat1 / 60.0);
      lon_ = (lod1 + lon1 / 60.0);

      double h = alt_ = stod(nmea.at(9));
      quality_ = stoi(nmea.at(6));
      num_satellite_ = stoi(nmea.at(7));
      geo_.set_llh_nmea_degrees(lat, lon, h);
      ROS_INFO("GGA is subscribed.");

      map_tools::MgrsConverter converter;
      double lat2, lon2, alt2;
      converter.jpxy2latlon(y_, x_, z_, 7, lat2, lon2, alt2);

      ofs << current_stamp << ","
          << std::fixed << std::setprecision(5) << x_ << ","
          << std::fixed << std::setprecision(5) << y_ << ","
          << std::fixed << std::setprecision(5) << z_ << ","
          << roll_ << ","
          << pitch_ << ","
          << yaw_ << ","
          << quality_ << ","
          << num_satellite_ << ","
          << std::setprecision(std::numeric_limits<double>::max_digits10) << lat_ << ","
          << std::setprecision(std::numeric_limits<double>::max_digits10) << lon_ << ","
          << alt_ << ","
          << std::setprecision(std::numeric_limits<double>::max_digits10) << lat2 << ","
          << std::setprecision(std::numeric_limits<double>::max_digits10) << lon2 << ","
          << alt2
          << std::endl;
    }
    else if(nmea.at(0) == "$GPRMC")
    {
      position_time_ = stoi(nmea.at(1));
      double lat = lat_ =  stod(nmea.at(3));
      double lon = lon_ = stod(nmea.at(5));
      double h = alt_ = 0.0;
      geo_.set_llh_nmea_degrees(lat, lon, h);
      ROS_INFO("GPRMC is subscribed.");
    }
  }catch (const std::exception &e)
  {
    ROS_WARN_STREAM("Message is invalid : " << e.what());
  }
}

void Nmea2TFPoseNode::callbackFromNmeaSentence(const nmea_msgs::Sentence::ConstPtr &msg)
{
  current_time_ = msg->header.stamp;
  convert(split(msg->sentence), msg->header.stamp);

  double timeout = 10.0;
  if (fabs(orientation_stamp_.toSec() - msg->header.stamp.toSec()) > timeout)
  {
    double dt = sqrt(pow(geo_.x() - last_geo_.x(), 2) + pow(geo_.y() - last_geo_.y(), 2));
    double threshold = 0.2;
    if (dt > threshold)
    {
      ROS_INFO("QQ is not subscribed. Orientation is created by atan2");
      createOrientation();
      publishPoseStamped();
      publishTF();
      last_geo_ = geo_;
    }
    return;
  }

  double e = 1e-2;
  if (fabs(orientation_time_ - position_time_) < e)
  {
    publishPoseStamped();
    publishTF();

    return;
  }
}

std::vector<std::string> split(const std::string &string)
{
  std::vector<std::string> str_vec_ptr;
  std::string token;
  std::stringstream ss(string);

  while (getline(ss, token, ','))
    str_vec_ptr.push_back(token);

  return str_vec_ptr;
}

}  // gnss_localizer