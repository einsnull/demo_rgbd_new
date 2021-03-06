/*
    Aug. 21 2018, He Zhang, hzhang8@vcu.edu 
    
    vio: tightly couple features [no depth, triangulated, measured] 
    with IMU integration 

*/

#pragma once

#include <vector>
#include <queue>
#include <Eigen/Core>
#include <Eigen/Dense>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <opencv2/core/eigen.hpp>
#include <sensor_msgs/PointCloud2.h>
#include <nav_msgs/Path.h>
#include "../utility/pointDefinition.h"
#include "imu_factor.h"
#include "parameters.h"
#include "tf/tf.h"

using namespace std; 

const static int WN = 1; 
const static int NOT_INITIED = -100000;

struct ip_M
{
    typedef enum{NO_DEPTH =0, DEPTH_MES, DEPTH_TRI, INVALID} DPT_TYPE;
    float ui, vi, uj, vj, s; // s responds to Xi = [ui,vi,1] * si
    int ind;
    DPT_TYPE v; 
};

class VIO
{
public:
    VIO(); 
    ~VIO(); 
    void clearState();

    void processIMU(double t, Eigen::Vector3d & linear_acceleration, Eigen::Vector3d& angular_velocity); 

    void processImage(sensor_msgs::PointCloud2ConstPtr& );
    void processDepthCloud(sensor_msgs::PointCloud2ConstPtr& );

    void solveOdometry(vector<ip_M>& , bool use_floor_plane = false); 
    void initialize(); 
    void associateFeatures(vector<ip_M>& vip); 
    void prepareNextLoop(); 

    void slideWindow(); 
    void priorOptimize(vector<ip_M>& ); 
    void afterOptimize(vector<ip_M>& );

    void setParameter();
    void setPointCloudAt(double); 
    void prepareForDisplay(vector<ip_M>& ipRelations);

    // remove outliers
    void removeOutliers(vector<ip_M>& ipRelations);
    void removeWrongTri(vector<ip_M>& ipRelations);
    void rejectByF(vector<ip_M>& ipRelations);

    // remove floor points 
    bool floor_detected();
    void removeFloorPts(boost::shared_ptr<pcl::PointCloud<pcl::PointXYZ> >& in, boost::shared_ptr<pcl::PointCloud<pcl::PointXYZI> >& out);
    Eigen::Quaterniond rotateToG(Eigen::Vector3d& fv);

    void processCurrDepthCloud(const sensor_msgs::PointCloud2ConstPtr& );
    queue<double> curr_pctime_buf; 
    queue<boost::shared_ptr<pcl::PointCloud<pcl::PointXYZI> > > curr_pc_buf;
    std::mutex m_curr_pc_buf;
    
    double mFloorZ; 
    double mFloorRange; 

    volatile bool mbInited;  
    bool mbStereo; // whether use stereo camera or other cameras like RGB-D camera 

    double mTimeLast; // current timestamp 
    double mTimeCurr; 
    
    boost::shared_ptr<pcl::PointCloud<ImagePoint> > mImgPTCurr; 
    boost::shared_ptr<pcl::PointCloud<ImagePoint> > mImgPTLast; 
    
    double mPCTime; // point cloud time 
    bool b_use_curr_single_pc; 
    boost::shared_ptr<pcl::PointCloud<pcl::PointXYZI> > mPC;
    boost::shared_ptr<pcl::PointCloud<pcl::PointXYZI> > mCurrSinglePC; // current frame's depth data
    boost::shared_ptr<pcl::KdTreeFLANN<pcl::PointXYZI> > mKDTree; 
    
    queue<double> pctime_buf; 
    queue<boost::shared_ptr<pcl::PointCloud<pcl::PointXYZI> > > pc_buf;
    std::mutex m_pc_buf;
    std::condition_variable con;

    double mZoomDis; // must equal to that in depth_handler 
    vector<float>  mvDptCurr; 
    vector<float>  mvDptLast; 
    
    vector<tf::Transform> mFtTransCurr; // record cam's pose when the features are first observed
    vector<tf::Transform> mFtTransLast; // record cam's pose when the features are first observed

    boost::shared_ptr<pcl::PointCloud<ImagePoint> > mFtObsCurr; // record the measurement 
    boost::shared_ptr<pcl::PointCloud<ImagePoint> > mFtObsLast; // record the measurement 
    
    tf::Transform mInitCamPose; // Initial campose 
    tf::Transform mLastPose; // camera pose for the mImgPTLast 
    tf::Transform mCurrPose; // camera pose for the mImgPTCurr 
    tf::Transform mCurrIMUPose; // current IMU pose  
    tf::Transform mTIC;  // transform from imu to camera 

    double mdisThresholdForTriangulation; // 

    // to display the 3d point cloud of the features 
    boost::shared_ptr<pcl::PointCloud<pcl::PointXYZ> > mImagePointsProj;
    boost::shared_ptr<pcl::PointCloud<pcl::PointXYZI> > mPCNoFloor;
    boost::shared_ptr<pcl::PointCloud<pcl::PointXYZ> > mPCFloor;
    boost::shared_ptr<pcl::PointCloud<pcl::PointXYZ> > mCurrPCFloor; // for debug 
    vector<ip_M> mPtRelations;    

    Eigen::Vector3d mg;
    Eigen::Matrix3d ric[NUM_OF_CAM]; 
    Eigen::Vector3d tic[NUM_OF_CAM]; 
    
    std_msgs::Header Headers[WN+1];
    Eigen::Vector3d Ps[WN+1]; 
    Eigen::Vector3d Vs[WN+1]; 
    Eigen::Matrix3d Rs[WN+1]; 
    Eigen::Vector3d Bas[WN+1]; 
    Eigen::Vector3d Bgs[WN+1]; 
    Eigen::Matrix3d R_imu; 

    Eigen::Matrix<double, 4, 1> Pls[WN+1]; // planes 
    volatile bool mbFirstFloorObserved; 
    
    bool mbFirstIMU; 
    Vector3d acc_0; 
    Vector3d gyr_0; 
 
    // IntegrationBase * tmp_pre_integration;  
    IntegrationBase * pre_integrations[WN+1]; 
    vector<double> dt_buf[WN+1]; 
    vector<Eigen::Vector3d> linear_acceleration_buf[WN+1]; 
    vector<Eigen::Vector3d> angular_velocity_buf[WN+1]; 
    
    int frame_count; 
    // FeatureManager f_manager; 
    
    double para_Pose[WN+1][SIZE_POSE];
    double para_SpeedBias[WN+1][SIZE_SPEEDBIAS];
    double para_Feature[NUM_OF_FEAT][SIZE_FEATURE];
    double para_Ex_Pose[NUM_OF_CAM][SIZE_POSE];
    // double para_Retrive_Pose[SIZE_POSE];

    bool mbStill; // whether camera is still, which is inferred by pixel disparity 

    nav_msgs::Path m_gt_path;  // groundtruth path 
    nav_msgs::Path m_est_path; // estimated path 

};
