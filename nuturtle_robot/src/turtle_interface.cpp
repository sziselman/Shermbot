/// \file turtle_interface.cpp
/// \brief contains a node called turtle_interface that will implement low-level control
/// and sensor routines. Reads in a twist and converts it to a specific wheel command that
/// makes the turtle move. Also, reads raw encoder data and outputs it as joint angles and
/// velocities.
///
/// PARAMETERS:
///             wheelRad : the radius of the robot's wheels
///             wheelBase : the distance between the robot's wheels
///             left_wheel_joint : the string used in publishing a joint_state message
///             right_wheel_joint : the string used in publishing a joint_state message
///             odom_frame_id : the string used in publishing a joint_state message
/// PUBLISHES:  wheel_cmd (nuturtlebot/WheelCommands)
///             joint_states (sensor_msgs/JointState)
/// SUBSCRIBES: cmd_vel (geometry_msgs/Twist)
///             sensor_data (nuturtlebot/SensorData)
/// SERVICES:

#include <ros/ros.h>

#include <geometry_msgs/Twist.h>
#include <sensor_msgs/JointState.h>
#include <nuturtlebot/WheelCommands.h>
#include <nuturtlebot/SensorData.h>

#include "rigid2d/rigid2d.hpp"
#include "rigid2d/diff_drive.hpp"

/******************
* Declare global variables
******************/
static ros::Publisher wheelCom_pub;
static ros::Publisher jointState_pub;

static rigid2d::DiffDrive ninjaTurtle;

static geometry_msgs::Twist twist_msg;
static nuturtlebot::SensorData sensor_data;

static bool michelangelo = true;

/******************
* Helper Functions
******************/
void twistCallback(const geometry_msgs::Twist msg);
void sensorCallback(const nuturtlebot::SensorData data);

int main(int argc, char* argv[])
{
    using namespace rigid2d;

    /**********************
    * Initialize the node & node handle
    **********************/
    ros::init(argc, argv, "turtle_interface");
    ros::NodeHandle n;

    /**********************
    * Define local variables
    **********************/
    int frequency = 100;
    double wheelRad, wheelBase;

    std::string left_wheel_joint, right_wheel_joint, odom_frame_id;

    double maxAngVel = 5.97; // rad/s
    n.getParam("wheel_radius", wheelRad);
    n.getParam("wheel_base", wheelBase);
    n.getParam("left_wheel_joint", left_wheel_joint);
    n.getParam("right_wheel_joint", right_wheel_joint);
    n.getParam("odom_frame_id", odom_frame_id);

    sensor_msgs::JointState joint_msg;
    nuturtlebot::WheelCommands wheelCom_msg;
    int encoderL, encoderR;

    /**********************
    * Define publisher, subscriber, services and clients
    **********************/
    wheelCom_pub = n.advertise<nuturtlebot::WheelCommands>("/wheel_cmd", frequency);
    jointState_pub = n.advertise<sensor_msgs::JointState>("/joint_states", frequency);

    ros::Subscriber twist_sub = n.subscribe("/cmd_vel", frequency, twistCallback);
    ros::Subscriber sensor_sub = n.subscribe("/sensor_data", frequency, sensorCallback);

    ros::Rate loop_rate(frequency);

    /*********************
    * Set initial parameters of the diff-drive robot
    * Create initial message to be published (all set to 0)
    *********************/
    ninjaTurtle = DiffDrive(wheelBase, wheelRad, 0.0, 0.0, 0.0, 0.0, 0.0);


    joint_msg.name.push_back(left_wheel_joint);
    joint_msg.name.push_back(right_wheel_joint);

    joint_msg.position.push_back(0.0);
    joint_msg.position.push_back(0.0);

    joint_msg.velocity.push_back(0.0);
    joint_msg.velocity.push_back(0.0);
    
    jointState_pub.publish(joint_msg);

    wheelCom_msg.left_velocity = 0;
    wheelCom_msg.right_velocity = 0;
    wheelCom_pub.publish(wheelCom_msg);

    ros::Time current_time = ros::Time::now();
    ros::Time last_time = ros::Time::now();

    while (ros::ok())
    {
        ros::spinOnce();
        current_time = ros::Time::now();

        /********************
        * Get desird twist from twist message
        ********************/
        Twist2D desiredTwist;
        desiredTwist.dth = twist_msg.angular.z;
        desiredTwist.dx = twist_msg.linear.x;
        desiredTwist.dy = twist_msg.linear.y;

        /********************
        * Read the encoder data to update robot config based on current wheel angles
        ********************/
        if (michelangelo)
        {
            encoderL = sensor_data.left_encoder;
            encoderR = sensor_data.right_encoder;
            michelangelo = false;
        }

        double leftAngle = (2.0 * PI / 4096.0) * double(sensor_data.left_encoder - encoderL);
        double rightAngle = (2.0 * PI / 4096.0) * double(sensor_data.right_encoder - encoderR);

        ninjaTurtle(leftAngle, rightAngle);

        /********************
        * Get wheel velocities required to achieve desired twist
        ********************/
        wheelVel velocities = ninjaTurtle.convertTwist(desiredTwist);
        
        // Checks to make sure wheel velocities do not exceed maximum speed
        if (velocities.uL > maxAngVel)
        {
            velocities.uL = maxAngVel;
        } else if (velocities.uL < -maxAngVel)
        {
            velocities.uL = -maxAngVel;
        }

        if (velocities.uR > maxAngVel)
        {
            velocities.uR = maxAngVel;
        } else if (velocities.uR < -maxAngVel)
        {
            velocities.uR = -maxAngVel;
        }

        /********************
        * Update configuration of the diff drive robot
        ********************/
        // double thLnew = ninjaTurtle.getThL() + (velocities.uL * (current_time - last_time).toSec());
        // double thRnew = ninjaTurtle.getThR() + (velocities.uR * (current_time - last_time).toSec());

        // ninjaTurtle(thLnew, thRnew);

        /********************
        * publish wheel_cmd message
        ********************/
        // converts the velocity to an integer value between -256 and 256 proportional to max rotational velocity
        int leftCommand = round(velocities.uL * (256 / maxAngVel));
        int rightCommand = round(velocities.uR * (256 / maxAngVel));

        wheelCom_msg.left_velocity = leftCommand;
        wheelCom_msg.right_velocity = rightCommand;
        wheelCom_pub.publish(wheelCom_msg);

        /******************
        * publish joint_states message
        ******************/
        joint_msg.header.stamp = current_time;
        joint_msg.header.frame_id = odom_frame_id;

        joint_msg.position[0] = ninjaTurtle.getThL();
        joint_msg.position[1] = ninjaTurtle.getThR();

        joint_msg.velocity[0] = velocities.uL;
        joint_msg.velocity[1] = velocities.uR;

        jointState_pub.publish(joint_msg);

        last_time = current_time;
        loop_rate.sleep();
    }
    return 0;
}

/// \brief twistCallback function
/// \param msg a geometry twist message
/// when cmd_vel message is received, it publishes wheel_cmd message
/// that makes the robot follow that twist
void twistCallback(const geometry_msgs::Twist msg)
{
    twist_msg = msg;
}

/// \brief sensorCallback function
/// \param data
/// when sensor_data message is received, it publishes joint_states
/// message to provide the angle and velocity of the wheels, based on
/// encoder data
void sensorCallback(const nuturtlebot::SensorData data)
{
    sensor_data = data;
}