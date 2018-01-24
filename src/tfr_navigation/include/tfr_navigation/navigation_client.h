/**
 *  Test Class for the navigation action server, eventuall will be enveloped
 *  into the executive node. 
 *
 *  Attaches to the node named "navigation_action_server" in the global
 *  namespace.
 *
 *  Subscribes to all of the usual topics needed by a simple action client
 *
 * */
#ifndef NAVIGATION_CLIENT_H
#define NAVIGATION_CLIENT_H
#include <ros/ros.h>
#include <ros/console.h>
#include <actionlib/client/simple_action_client.h>
#include <boost/bind.hpp>
#include <cstdint>
#include <tfr_msgs/NavigationAction.h>
class NavigationClient
{
    public:
        NavigationClient(std::string action_name);
        ~NavigationClient() {} 

        //delete generated methods
        NavigationClient(const NavigationClient&) = delete;
        NavigationClient& operator=(const NavigationClient&) = delete;
        NavigationClient(NavigationClient&&) = delete;
        NavigationClient& operator=(NavigationClient&&) = delete;
        void navigate_to_mining();

    private:
        void navigate(uint8_t action);
        void feedback(const tfr_msgs::NavigationFeedbackConstPtr& feedback);
        void finished(const actionlib::SimpleClientGoalState &state, 
                const tfr_msgs::NavigationResultConstPtr &result);
        using Client = actionlib::SimpleActionClient<tfr_msgs::NavigationAction>; 
        Client client;
};
#endif
