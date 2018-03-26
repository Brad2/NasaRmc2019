/** * controller.cpp * 
 * This class is in charge of handling the physical hardware interface with
 * the robot itself, and is started by the controller_launcher node.
 */
#include "robot_interface.h"

using hardware_interface::JointStateHandle;
using hardware_interface::JointHandle;

namespace tfr_control
{
    /*
     * Creates the robot interfaces spins up all the joints and registers them
     * with their relevant interfaces
     * */
    RobotInterface::RobotInterface(ros::NodeHandle &n, bool fakes, 
            const double *lower_lim, const double *upper_lim) :
        pwm{},
        arduino{n.subscribe("/sensors/arduino", 5, &RobotInterface::readArduino, this)},
        use_fake_values{fakes}, lower_limits{lower_lim},
        upper_limits{upper_lim}, drivebase_v0{std::make_pair(0,0)},
        last_update{ros::Time::now()}

    {
        // Note: the string parameters in these constructors must match the
        // joint names from the URDF, and yaml controller description. 

        // Connect and register each joint with appropriate interfaces at our
        // layer
        registerTreadJoint("left_tread_joint", Joint::LEFT_TREAD);
        registerTreadJoint("right_tread_joint", Joint::RIGHT_TREAD);
        registerBinJoint("bin_joint", Joint::BIN); 
        registerArmJoint("turntable_joint", Joint::TURNTABLE);
        registerArmJoint("lower_arm_joint", Joint::LOWER_ARM);
        registerArmJoint("upper_arm_joint", Joint::UPPER_ARM);
        registerArmJoint("scoop_joint", Joint::SCOOP);
        //register the interfaces with the controller layer
        registerInterface(&joint_state_interface);
        registerInterface(&joint_effort_interface);
        registerInterface(&joint_position_interface);
        pwm.enablePWM(true);
    }

    /*
     * Reads from our hardware and populates from shared memory.  
     *
     * Information that is not that are not expicity needed by our controllers 
     * are written to some safe sensible default (usually 0).
     *
     * A couple of our logical joints are controlled by two actuators and read
     * by multiple potentiometers. For the purpose of populating information for
     * control I take the average of the two positions.
     * */
    void RobotInterface::read() 
    {
        //Grab the neccessary data
        tfr_msgs::ArduinoReading reading;
        if (latest_arduino != nullptr)
            reading = *latest_arduino;

        //LEFT_TREAD
        position_values[static_cast<int>(Joint::LEFT_TREAD)] = 0;
        velocity_values[static_cast<int>(Joint::LEFT_TREAD)] = reading.tread_left_vel;
        effort_values[static_cast<int>(Joint::LEFT_TREAD)] = 0;

        //RIGHT_TREAD
        position_values[static_cast<int>(Joint::RIGHT_TREAD)] = 0;
        velocity_values[static_cast<int>(Joint::RIGHT_TREAD)] = reading.tread_right_vel;
        effort_values[static_cast<int>(Joint::RIGHT_TREAD)] = 0;

        if (!use_fake_values)
        {
            //TURNTABLE
            position_values[static_cast<int>(Joint::TURNTABLE)] = reading.arm_turntable_pos;
            velocity_values[static_cast<int>(Joint::TURNTABLE)] = 0;
            effort_values[static_cast<int>(Joint::TURNTABLE)] = 0;

            //LOWER_ARM
            position_values[static_cast<int>(Joint::LOWER_ARM)] = 
                (reading.arm_lower_left_pos+ reading.arm_lower_right_pos)/2;
            velocity_values[static_cast<int>(Joint::LOWER_ARM)] = 0;
            effort_values[static_cast<int>(Joint::LOWER_ARM)] = 0;

            //UPPER_ARM
            position_values[static_cast<int>(Joint::UPPER_ARM)] = reading.arm_upper_pos;
            velocity_values[static_cast<int>(Joint::UPPER_ARM)] = 0;
            effort_values[static_cast<int>(Joint::UPPER_ARM)] = 0;

            //SCOOP
            position_values[static_cast<int>(Joint::SCOOP)] = reading.arm_scoop_pos;
            velocity_values[static_cast<int>(Joint::SCOOP)] = 0;
            effort_values[static_cast<int>(Joint::SCOOP)] = 0;
        }

        //BIN
        position_values[static_cast<int>(Joint::BIN)] = 
            (reading.bin_left_pos + reading.bin_right_pos)/2;
        velocity_values[static_cast<int>(Joint::BIN)] = 0;
        effort_values[static_cast<int>(Joint::BIN)] = 0;

    }

    /*
     * Writes command values from our controllers to our motors and actuators.
     *
     * Takes in command values from the controllers and these values are scaled
     * to pwm outputs and written to the right place. There are some edge cases
     * for twin actuators, which are controlled as if they are one joint. 
     *
     * The controller gives a command value to move them as one, then we scale
     * our pwm outputs to move them back into sync if they get out of wack.
     * */
    void RobotInterface::write() 
    {
        //Grab the neccessary data
        tfr_msgs::ArduinoReading reading;
        if (latest_arduino != nullptr)
            reading = *latest_arduino;

        double signal;
        if (use_fake_values) //test code  for working with rviz simulator
        {
            // ADAM make sure to update this index when you need to
            for (int i = 3; i < JOINT_COUNT; i++) 
            {
                position_values[i] = command_values[i];
                // If this joint has limits, clamp the range down
                if (abs(lower_limits[i]) >= 1E-3 || abs(upper_limits[i]) >= 1E-3) 
                {
                    position_values[i] =
                        std::max(std::min(position_values[i],
                                    upper_limits[i]), lower_limits[i]);
                }
                ROS_INFO("command %f", command_values[i]);
            }
        }
        else  // we are working with the real arm
        {
            //TURNTABLE
            signal = turntableAngleToPWM(command_values[static_cast<int>(Joint::TURNTABLE)],
                        position_values[static_cast<int>(Joint::TURNTABLE)]);
            pwm.set(PWMInterface::Address::ARM_TURNTABLE, signal);

            //LOWER_ARM
            auto twin_signal = twinAngleToPWM(command_values[static_cast<int>(Joint::LOWER_ARM)],
                        reading.arm_lower_left_pos,
                        reading.arm_lower_right_pos);
            pwm.set(PWMInterface::Address::ARM_LOWER_LEFT, twin_signal.first);
            pwm.set(PWMInterface::Address::ARM_LOWER_RIGHT, twin_signal.second);

            //UPPER_ARM
            signal = angleToPWM(command_values[static_cast<int>(Joint::UPPER_ARM)],
                        position_values[static_cast<int>(Joint::UPPER_ARM)]);
            pwm.set(PWMInterface::Address::ARM_UPPER, signal);

            //SCOOP
            signal = angleToPWM(command_values[static_cast<int>(Joint::SCOOP)],
                        position_values[static_cast<int>(Joint::SCOOP)]);
            pwm.set(PWMInterface::Address::ARM_SCOOP, signal);
        }

        //LEFT_TREAD
        signal = drivebaseVelocityToPWM(command_values[static_cast<int>(Joint::LEFT_TREAD)],
                    drivebase_v0.first);
        pwm.set(PWMInterface::Address::TREAD_LEFT, signal);

        //RIGHT_TREAD
        signal =
            drivebaseVelocityToPWM(command_values[static_cast<int>(Joint::RIGHT_TREAD)],
                    drivebase_v0.second);
        pwm.set(PWMInterface::Address::TREAD_RIGHT, signal);

        //BIN
        auto twin_signal = twinAngleToPWM(command_values[static_cast<int>(Joint::BIN)],
                    reading.bin_left_pos,
                    reading.bin_left_pos);
        pwm.set(PWMInterface::Address::BIN_LEFT, twin_signal.first);
        pwm.set(PWMInterface::Address::BIN_RIGHT, twin_signal.second);
        
        //UPKEEP
        last_update = ros::Time::now();
        drivebase_v0.first = velocity_values[static_cast<int>(Joint::LEFT_TREAD)];
        drivebase_v0.second = velocity_values[static_cast<int>(Joint::RIGHT_TREAD)];
    }

    /*
     * Resets the commands to a safe neutral state
     * Tells the treads to stop moving, and the arm to hold position
     * */
    void RobotInterface::clearCommands()
    {
        //LEFT_TREAD
        command_values[static_cast<int>(Joint::LEFT_TREAD)] = 0;

        //RIGHT_TREAD
        command_values[static_cast<int>(Joint::RIGHT_TREAD)] = 0;

        //TURNTABLE
        command_values[static_cast<int>(Joint::TURNTABLE)] = 
            position_values[static_cast<int>(Joint::TURNTABLE)];

        //LOWER_ARM
        command_values[static_cast<int>(Joint::LOWER_ARM)] =
            position_values[static_cast<int>(Joint::LOWER_ARM)];
        //UPPER_ARM
        command_values[static_cast<int>(Joint::UPPER_ARM)] =
            position_values[static_cast<int>(Joint::UPPER_ARM)];
        //SCOOP
        command_values[static_cast<int>(Joint::SCOOP)] =
            position_values[static_cast<int>(Joint::SCOOP)];
        //BIN
        command_values[static_cast<int>(Joint::BIN)] =
            position_values[static_cast<int>(Joint::BIN)];
    }

    /*
     * Returns if the bin is extended or not
     * */
    bool RobotInterface::isBinExtended()
    {
        double goal = 0.785398;
        double tolerance = 0.01;
        return goal - position_values[static_cast<int>(Joint::BIN)] < tolerance;
    }



    /*
     * Register this joint with each neccessary hardware interface
     * */
    void RobotInterface::registerTreadJoint(std::string name, Joint joint) 
    {
        auto idx = static_cast<int>(joint);
        //give the joint a state
        JointStateHandle state_handle(name, &position_values[idx],
            &velocity_values[idx], &effort_values[idx]);
        joint_state_interface.registerHandle(state_handle);

        //allow the joint to be commanded
        JointHandle handle(state_handle, &command_values[idx]);
        joint_effort_interface.registerHandle(handle);
    }

    /*
     * Register this joint with each neccessary hardware interface
     * */
    void RobotInterface::registerBinJoint(std::string name, Joint joint) 
    {
        auto idx = static_cast<int>(joint);
        //give the joint a state
        JointStateHandle state_handle(name, &position_values[idx],
            &velocity_values[idx], &effort_values[idx]);
        joint_state_interface.registerHandle(state_handle);

        //allow the joint to be commanded
        JointHandle handle(state_handle, &command_values[idx]);
        joint_effort_interface.registerHandle(handle);
    }

    /*
     * Register this joint with each neccessary hardware interface
     * */
    void RobotInterface::registerArmJoint(std::string name, Joint joint) 
    {
        auto idx = static_cast<int>(joint);
        //give the joint a state
        JointStateHandle state_handle(name, &position_values[idx],
            &velocity_values[idx], &effort_values[idx]);
        joint_state_interface.registerHandle(state_handle);

        //allow the joint to be commanded
        JointHandle handle(state_handle, &command_values[idx]);
        joint_position_interface.registerHandle(handle);
    }

    /*
     * Input is angle desired/measured and output is in raw pwm frequency.
     * */
    double RobotInterface::angleToPWM(const double &desired, const double &actual)
    {
        //we don't anticipate this changing very much keep at method level
        double angle_tolerance = 0.01;
        double difference = desired - actual;
        if (abs(difference) > angle_tolerance)
            return (difference < 0) ? -1 : 1;
        return 0;
    }

    /*
     * Input is angle desired/measured of a twin acutuator joint and output is
     * in raw pwm frequency for both of them. The actuator further ahead get's
     * scaled down.
     * */
    std::pair<double,double> RobotInterface::twinAngleToPWM(const double &desired, 
            const double &actual_left, const double &actual_right)
    {
        //we don't anticipate these changing very much keep at method level
        double  total_angle_tolerance = 0.01,
                individual_angle_tolerance = 0.01,
                scaling_factor = .9, 
                difference = desired - (actual_left + actual_right)/2;
        if (abs(difference) > total_angle_tolerance)
        {
            int direction = (difference < 0) ? -1 : 1;
            double delta = actual_left - actual_right;
            double cmd_left = direction, cmd_right = direction;
            if (abs(delta) > individual_angle_tolerance)
            {
                if (actual_left > actual_right)
                    cmd_left *= scaling_factor;
                else
                    cmd_right *= scaling_factor;
            }
            return std::make_pair(cmd_left, cmd_right);
        }
        return std::make_pair(0,0);
    }
    
    /*
     * Input is angle desired/measured of turntable and output is in raw pwm frequency.
     * */
    double RobotInterface::turntableAngleToPWM(const double &desired, const double &actual)
    {
        //we don't anticipate this changing very much keep at method level
        double angle_tolerance = 0.01;
        double difference = desired - actual;
        if (abs(difference) > angle_tolerance)
            return (difference < 0) ? -0.8 : 0.8;
        return 0;
    }

    /*
     * Takes in a velocity, and converts it to pwm for the drivebase.
     * Velocity is in meters per second, and output is in raw pwm frequency.
     * Scaled to match the values expected by pwm interface
     * NOTE we have a safety limit here of 1 m/s^2 any more and it will snap a
     * shaft
     * */
    double RobotInterface::drivebaseVelocityToPWM(const double& v_1, const double& v_0)
    {
        //limit for acceleration
        double vel = v_1, d_v = v_1 - v_0 , d_t = (ros::Time::now()- last_update).toSec();
        double max_a = 1;

        /*
         * d_v/d_t = max_a
         * d_v = max_a * d_t && d_v = v_1 - v_0
         * v_1 = max_a * d_t - v_0
         * */
        if (abs(d_v/d_t) > max_a)
        {
            if (d_v < 0)
                max_a = -1;
            vel = max_a * d_t - v_0;
        }
        
        //limit for max velocity
        //we don't anticipate this changing very much keep at method level
        double max_vel = 1;
        int sign = (vel < 0) ? -1 : 1;
        double magnitude = std::min((vel*sign)/max_vel, 1.0);
        return sign * magnitude;
    }

    /*
     * Callback for our encoder subscriber
     * */
    void RobotInterface::readArduino(const tfr_msgs::ArduinoReadingConstPtr &msg)
    {
        latest_arduino = msg;
    }

}
