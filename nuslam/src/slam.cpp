/// \file slam.cpp
/// \brief contains a node called odometry that will publish odometry messages in a standard ROS way
///
/// PARAMETERS:
///     wheel_base (double) : The distance between wheels
///     wheel_radius (double)   : The radius of both wheels
///     odom_frame_id   : The name of the odometry tf frame
///     body_frame_id   : The name of the body tf frame
///     left_wheel_joint    : The name of the left wheel joint
///     right_wheel_joint   : The name of the right wheel joint
/// PUBLISHES: odom (nav_msgs/Odometry)
/// SUBSCRIBES: joint_states (sensor_msgs/JointState)
/// SERVICES: set_pose : Sets the pose of the turtlebot's configuration

#include <ros/ros.h>

#include <rigid2d/set_pose.h>

#include <nav_msgs/Odometry.h>
#include <nav_msgs/Path.h>

#include <geometry_msgs/TransformStamped.h>
#include <geometry_msgs/Quaternion.h>
#include <geometry_msgs/PoseStamped.h>

#include <visualization_msgs/MarkerArray.h>
#include <visualization_msgs/Marker.h>

#include <tf2_ros/transform_broadcaster.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>

#include <sensor_msgs/JointState.h>

#include <rigid2d/rigid2d.hpp>
#include <rigid2d/diff_drive.hpp>

#include <nuslam/slam_library.hpp>

#include <armadillo>
#include <string>
#include <iostream>

/****************
 * Declare global variables
 * *************/
static rigid2d::DiffDrive ninjaTurtle;
static rigid2d::DiffDrive teenageMutant;

static sensor_msgs::JointState joint_state_msg;
static visualization_msgs::MarkerArray marker_array;

static double wheelBase, wheelRad;
static bool donatello = false;

/****************
 * Helper functions
 * *************/
void jointStateCallback(const sensor_msgs::JointState msg);
void sensorCallback(const visualization_msgs::MarkerArray array);
bool setPose(rigid2d::set_pose::Request & req, rigid2d::set_pose::Response &res);

/****************
 * Main Function
 * *************/

int main(int argc, char* argv[])
{
    using namespace rigid2d;
    using namespace slam_library;
    using namespace arma;

    /****************
     * Initialize the node & node handle
     * *************/
    ros::init(argc, argv, "slam");
    ros::NodeHandle n;

    /****************
     * Declare local variables
     * *************/
    std::string odom_frame_id, body_frame_id, left_wheel_joint, right_wheel_joint, world_frame_id;
    std::vector<double> tube1_loc;
    std::vector<double> tube2_loc;
    std::vector<double> tube3_loc;
    std::vector<double> tube4_loc;
    std::vector<double> rVec;
    std::vector<double> qVec;
    double tubeRad;

    int frequency = 100;
    int num = 4;              // number of landmarks

    nav_msgs::Path odom_path;
    nav_msgs::Path slam_path;
    tf2_ros::TransformBroadcaster odom_broadcaster;

    /****************
     * Reading parameters from parameter server
     * *************/
    n.getParam("wheel_base", wheelBase);
    n.getParam("wheel_radius", wheelRad);
    n.getParam("odom_frame_id", odom_frame_id);
    n.getParam("body_frame_id", body_frame_id);
    n.getParam("left_wheel_joint", left_wheel_joint);
    n.getParam("right_wheel_joint", right_wheel_joint);
    n.getParam("world_frame_id", world_frame_id);
    n.getParam("tube1_location", tube1_loc);
    n.getParam("tube2_location", tube2_loc);
    n.getParam("tube3_location", tube3_loc);
    n.getParam("tube4_location", tube4_loc);
    n.getParam("tube_radius", tubeRad);
    n.getParam("R", rVec);
    n.getParam("Q", qVec);

    /****************
     * Define publisher, subscriber, services and clients
     * *************/
    ros::Publisher odom_pub = n.advertise<nav_msgs::Odometry>("odom", frequency);
    ros::Publisher path_pub = n.advertise<nav_msgs::Path>("/real_path", frequency);

    ros::Subscriber joint_sub = n.subscribe("/joint_states", frequency, jointStateCallback);
    ros::Subscriber sensor_sub = n.subscribe("/fake_sensor", frequency, sensorCallback);
    ros::ServiceServer setPose_service = n.advertiseService("set_pose", setPose);
    ros::ServiceClient setPose_client = n.serviceClient<rigid2d::set_pose>("set_pose");

    ros::Rate loop_rate(frequency);

    /****************
     * Set initial parameters of the differential drive robot to 0
     * *************/
    ninjaTurtle = DiffDrive(wheelBase, wheelRad, 0.0, 0.0, 0.0, 0.0, 0.0);
    teenageMutant = DiffDrive(wheelBase, wheelRad, 0.0, 0.0, 0.0, 0.0, 0.0);

    /***************
     * Create Extended Kalman filter object
     * ************/
    colvec mapState(2*num);
    mapState(0) = tube1_loc[0];
    mapState(1) = tube1_loc[1];
    mapState(2) = tube2_loc[0];
    mapState(3) = tube2_loc[1];
    mapState(4) = tube3_loc[0];
    mapState(5) = tube3_loc[1];
    mapState(6) = tube4_loc[0];
    mapState(7) = tube4_loc[1];

    colvec robotState(3);
    robotState(0) = ninjaTurtle.getTh();
    robotState(1) = ninjaTurtle.getX();
    robotState(2) = ninjaTurtle.getY();

    mat Q(3, 3);
    for (int i = 0; i < int(qVec.size()); ++i)
    {
        Q(i) = qVec[i];
    }

    mat R(2, 2);
    for (int j = 0; j < int(rVec.size()); ++j)
    {
        R(j) = rVec[j];
    }
    
    ExtendedKalman raphael = ExtendedKalman(robotState, mapState, Q, R);

    while(ros::ok())
    {
        ros::spinOnce();

        ros::Time current_time = ros::Time::now();

        /****************
         * Get the velocities from new wheel angles and update configuration
         * *************/
        Twist2D twist_vel = ninjaTurtle.getTwist(joint_state_msg.position[0], joint_state_msg.position[1]);

        ninjaTurtle(joint_state_msg.position[0], joint_state_msg.position[1]);

        /****************
         * If a marker array is received
         * *************/
        if (donatello)
        {
            // get velocities from new wheel angles and update config
            Twist2D slam_twist = teenageMutant.getTwist(joint_state_msg.position[0], joint_state_msg.position[1]);
            teenageMutant(joint_state_msg.position[0], joint_state_msg.position[1]);

            // predict: update the estimate using the model
            raphael.predict(slam_twist);
            
            // for loop that goes through each marker that was measured
            for (auto marker : marker_array.markers)
            {
                // put the marker x, y location in range-bearing form
                colvec rangeBearing = RangeBearing(marker.pose.position.x, marker.pose.position.y, teenageMutant.getTh());

                // compute theoretical measurement, given the current state estimate
                raphael.update(marker.id, rangeBearing);
            }
        }

        /****************
         * Publish a nav_msgs/Path showing the trajectory of the robot according only to wheel odometry
         * *************/
        geometry_msgs::PoseStamped odom_poseStamp;
        odom_path.header.stamp = current_time;
        odom_path.header.frame_id = world_frame_id;
        odom_poseStamp.pose.position.x = ninjaTurtle.getX();
        odom_poseStamp.pose.position.y = ninjaTurtle.getY();
        odom_poseStamp.pose.orientation.z = ninjaTurtle.getTh();

        odom_path.poses.push_back(odom_poseStamp);
        path_pub.publish(odom_path);

        /****************
         * Create quaternion from yaw
         * *************/
        tf2::Quaternion odom_quater;
        odom_quater.setRPY(0, 0, ninjaTurtle.getTh());
        geometry_msgs::Quaternion odom_quat = tf2::toMsg(odom_quater);

        /****************
         * Publish the transform over tf
         * *************/
        geometry_msgs::TransformStamped odom_trans;
        odom_trans.header.stamp = current_time;
        odom_trans.header.frame_id = odom_frame_id;
        odom_trans.child_frame_id = body_frame_id;

        odom_trans.transform.translation.x = ninjaTurtle.getX();
        odom_trans.transform.translation.y = ninjaTurtle.getY();
        odom_trans.transform.translation.z = 0.0;
        odom_trans.transform.rotation = odom_quat;

        odom_broadcaster.sendTransform(odom_trans);

        /***************
         * Publish the odometry message over ROS
         * ************/
        nav_msgs::Odometry odom_msg;
        odom_msg.header.stamp = current_time;
        odom_msg.header.frame_id = odom_frame_id;
        odom_msg.pose.pose.position.x = ninjaTurtle.getX();
        odom_msg.pose.pose.position.y = ninjaTurtle.getY();
        odom_msg.pose.pose.position.z = 0.0;
        odom_msg.pose.pose.orientation = odom_quat;

        odom_msg.child_frame_id = body_frame_id;
        odom_msg.twist.twist.linear.x = twist_vel.dx;
        odom_msg.twist.twist.linear.y = twist_vel.dy;
        odom_msg.twist.twist.linear.z = twist_vel.dth;

        odom_pub.publish(odom_msg);
        
        loop_rate.sleep();
    }
    return 0;
}

/// \brief callback function for subscriber to the sensor message
/// \param msg : the visualization message
void sensorCallback(const visualization_msgs::MarkerArray array)
{
    marker_array = array;
}

/// \brief callback function for subscriber to joint state message
/// Sends an odometry message and broadcasts a tf transform to update the configuration of the robot
/// \param msg : the joint state message
void jointStateCallback(const sensor_msgs::JointState msg)
{
    joint_state_msg = msg;
    donatello = true;
}

/// \brief setPose function for set_pose service
/// The service request provides the configuration of the robot
/// The location of the odometry is reset so the robot is at the requested configuration
/// \param req : The service request
/// \param res : The service reponse
/// \return true
bool setPose(rigid2d::set_pose::Request &req, rigid2d::set_pose::Response &res)
{
    using namespace rigid2d;

    /****************************
    * Configuration of the robot
    ****************************/
    double xNew = req.x;
    double yNew = req.y;
    double thNew = req.th;

    /****************************
    * Location of odometry reset so robot is at requested location
    * Replaces ninjaTurtle with a new configuration
    ****************************/
    ninjaTurtle = DiffDrive(wheelBase, wheelRad, xNew, yNew, thNew, 0.0, 0.0);

    return true;
}