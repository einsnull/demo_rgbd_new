/*
    Node interface 
*/

#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/PointCloud2.h>
#include <cv_bridge/cv_bridge.h>
#include "feature_tracker.h"
#include "../utility/pointDefinition.h"
#include "../utility/tic_toc.h"

CTrackerParam track_param;
CFeatureTracker feat_tracker(track_param); 

ros::Publisher *imagePointsLastPubPointer;
ros::Publisher *imageShowPubPointer;

int pub_count = 0;
double first_pub_time = 0; 

void imgCallback(const sensor_msgs::Image::ConstPtr& imageData);

int main(int argc, char* argv[]) 
{
    ros::init(argc, argv, "feature_tracking");
    ros::NodeHandle nh;
    ros::NodeHandle np("~"); 
    // ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Debug);
    ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Info);

    // ros::Subscriber imgDataSub = nh.subscribe<sensor_msgs::Image>("/image/raw", 1, imgCallback); 
    // ros::Subscriber imgDataSub = nh.subscribe<sensor_msgs::Image>("/camera/rgb/image_rect", 1, imgCallback); 
    ros::Subscriber imgDataSub = nh.subscribe<sensor_msgs::Image>("/cam0/color", 1, imgCallback); 
    // ros::Subscriber imgDataSub = nh.subscribe<sensor_msgs::Image>("/rgb", 1, imgCallback); 


    ros::Publisher tracked_features_pub = nh.advertise<sensor_msgs::PointCloud2>("/image_points_last", 5); 
    imagePointsLastPubPointer = &tracked_features_pub;
    
    ros::Publisher imageShowPub = nh.advertise<sensor_msgs::Image>("/image/show", 1);
    imageShowPubPointer = &imageShowPub;

    string param_file(""); 
    np.param("config_file", param_file, param_file); 
    if(param_file != "")
    { 
	feat_tracker.readParam(param_file);
	feat_tracker.mParam.printParam();
    }

    ros::spin();

    return 1; 
}


void imgCallback(const sensor_msgs::Image::ConstPtr& _img)
{
    cv_bridge::CvImageConstPtr ptr = cv_bridge::toCvCopy(_img, sensor_msgs::image_encodings::MONO8);

    cv::Mat img_mono = ptr->image; 
    double img_time = _img->header.stamp.toSec(); 
    TicToc t_s;
    static double sum_ft_t = 0; 
    static int sum_ft_cnt = 0; 
    if(feat_tracker.handleImage(img_mono, img_time))
    {
    	double whole_t = t_s.toc();
	 	ROS_WARN("feature_track_node.cpp: feature track cost %f ms", whole_t); 
	    sum_ft_t += whole_t; 
	    ROS_WARN("feature_track_node.cpp: average feature track cost %f ms", sum_ft_t/(++sum_ft_cnt));

	// publish msg 
	pcl::PointCloud<ImagePoint>::Ptr imagePointsLast(new pcl::PointCloud<ImagePoint>());
	imagePointsLast->points.resize(feat_tracker.mvPreImagePts.size()); 
	ROS_WARN("feature_tracker_node: track %d number of features", feat_tracker.mvPreImagePts.size());
	for(int i=0; i<feat_tracker.mvPreImagePts.size(); i++)
	{
	    ImagePoint& pt = imagePointsLast->points[i]; 
	    CFeatureTracker::ImagePoint& pt1 = feat_tracker.mvPreImagePts[i]; 
	    pt.u = pt1.u;  pt.v = pt1.v; pt.ind = pt1.ind; 
	}
	
	sensor_msgs::PointCloud2 imagePointsLast2;
	pcl::toROSMsg(*imagePointsLast,  imagePointsLast2); 
	imagePointsLast2.header.stamp = ros::Time().fromSec(feat_tracker.mTimePre); 
	
	imagePointsLastPubPointer->publish(imagePointsLast2); 
	// cout <<"feature_track_node: msg at" <<std::fixed<<imagePointsLast2.header.stamp.toSec()<<" first and last pt: "<<imagePointsLast->points[0].u<<" "<<imagePointsLast->points[imagePointsLast->points.size()-1].u<<" "<<imagePointsLast->points[imagePointsLast->points.size()-1].v<<endl;

	// ROS_WARN("Feature_tracking: publish %d at fps %f", ++pub_count, 1.*pub_count/(img_time-first_pub_time));

	
	// show img 
	if(feat_tracker.mbSendImgForShow)
	{
	    cv_bridge::CvImage bridge; 
	    bridge.image = feat_tracker.mImgShow; 
	    bridge.encoding = "mono8"; 
	    imageShowPubPointer->publish(bridge.toImageMsg()); 
	}
    }else
    {
	first_pub_time = img_time; 
    }
}

