/*
    Aug. 13 2018, He Zhang, hzhang8@vcu.edu 

    Implement Visual Odometry 

*/


#include "visual_odometry.h"
#include "stereo_triangulate.h"
#include "../Utility/utility.h"

struct ip_M
{
    typedef enum{NO_DEPTH =0, DEPTH_MES, DEPTH_TRI, INVALID} DPT_TYPE;
    float ui, vi, uj, vj, s; // s responds to Xi = [ui,vi,1] * si
    int nid;
    DPT_TYPE v; 
};

struct Jacob_Q{
    union 
    {
	struct
	{
	    float de_dx; // tx 
	    float de_dy; // ty 
	    float de_dz; // tz
	    float de_dq1; // theta1  
	    float de_dq2; // theta2
	    float de_dq3; // theta3
	    float de_dq4;
	};
	float de[7]; 
    };
    float err;
};


VisualOdometry::VisualOdometry():
mTimeLast(0),
mTimeCurr(0),
mImgPTLast(new pcl::PointCloud<ImagePoint>),
mImgPTCurr(new pcl::PointCloud<ImagePoint>),
mPCTime(0),
mPC(new pcl::PointCloud<pcl::PointXYZI>),
mKDTree(new pcl::KdTreeFLANN<pcl::PointXYZI>),
mZoomDis(10.),
mdisThresholdForTriangulation(1.) // 
{}
VisualOdometry::~VisualOdometry()
{
    if(mvDptCurr) delete mvDptCurr; 
    if(mvDptLast) delete mvDptLast; 
}

void VisualOdometry::imagePointsHandler(const sensor_msgs::PointCloud2ConstPtr& imagePoints2)
{
    // TODO: Include IMU's motion estimation 
    
    // handle last imgpt and currpt
    mTimeLast = mTimeCurr; 
    mTimeCurr = imagePoints2->header.stamp.toSec(); 
    
    mImgPTLast.swap(mImgPTCurr); 
    mImgPTCurr->clear(); 
    
    pcl::fromROSMsg(*imagePoints2, *mImgPTCurr); 
    
    int imgPTLastNum = mImgPTLast->points.size(); 
    int imgPTCurrNum = mImgPTCurr->points.size();
    
    // handle 
    pcl::PointCloud<ImagePoint>::Ptr startPointsTemp = startPointsLast;
    startPointsLast = startPointsCur;
    startPointsCur = startPointsTemp;

    pcl::PointCloud<pcl::PointXYZHSV>::Ptr startTransTemp = startTransLast;
    startTransLast = startTransCur;
    startTransCur = startTransTemp;

    // handle features' depth 
    std::vector<float>* ipDepthTemp = mvDptLast;
    mvDptLast = mvDptCurr;
    mvDptCurr = ipDepthTemp;

    // find out the depth for the matched points in LastImg 
    pcl::PointXYZI ips; 
    // pcl::PointXYZHSV ipr; 
    // x,y : u,v in LastImg z,h : u,v in CurrImg, 
    // s: distance along the ray, v: 1 has depth, 2 triangulate 0 no depth  
    // pcl::PointCloud<pcl::PointXYZHSV>::Ptr ipRelations(new pcl::PointCloud<pcl::PointXYZHSV>());
    vector<ip_M> ipRelations; 
    ip_M ipr; 
    // std::vector<int> ipInd;
    
    int j = 0;
    for(int i=0; i<imgPTLastNum; i++)
    {
	bool ipFound = false; 
	for(; j<imgPTCurrNum; j++)
	{
	    if(mImgPTLast->points[i].ind == mImgPTCurr->points[j].ind) 
	    {
		ipFound = true; 
		break; 
	    }
	    if(mImgPTLast->points[j].ind > mImgPTLast->points[i].ind)
		break; 
	}
	
	if(ipFound)
	{
	    // normalized point 
	    ipr.ui = mImgPTLast->points[i].u; 
	    ipr.vi = mImgPTLast->points[i].v; 
	    ipr.uj = mImgPTCurr->points[j].u; 
	    ipr.vj = mImgPTCurr->points[j].v; 

	    ips.x = mZoomDis * ipr.ui; 
	    ips.y = mZoomDis * ipr.vi; 
	    ips.z = mZoomDis; 

	    if(mPC->points.size() > 20)
	    {
		// for kd search 
		std::vector<int> pointSearchInd;
		std::vector<float> pointSearchSqrDis;

		mKDTree->nearestKDSearch(ips, 3, pointSearchInd, pointSearchSqrDis); 
		double minDepth, maxDepth; 
		if(pointSearchInd[0] < 0.5 && pointSearchInd.size() == 3)
		{
		    pcl::PointXYZI depthPoint = mPC->points[pointSearchInd[0]];
		    double x1 = depthPoint.x * depthPoint.intensity / mZoomDis;
		    double y1 = depthPoint.y * depthPoint.intensity / mZoomDis;
		    double z1 = depthPoint.intensity;
		    minDepth = z1;
		    maxDepth = z1;

		    depthPoint = mPC->points[pointSearchInd[1]];
		    double x2 = depthPoint.x * depthPoint.intensity / mZoomDis;
		    double y2 = depthPoint.y * depthPoint.intensity / mZoomDis;
		    double z2 = depthPoint.intensity;
		    minDepth = (z2 < minDepth)? z2 : minDepth;
		    maxDepth = (z2 > maxDepth)? z2 : maxDepth;

		    depthPoint = mPC->points[pointSearchInd[2]];
		    double x3 = depthPoint.x * depthPoint.intensity / mZoomDis;
		    double y3 = depthPoint.y * depthPoint.intensity / mZoomDis;
		    double z3 = depthPoint.intensity;
		    minDepth = (z3 < minDepth)? z3 : minDepth;
		    maxDepth = (z3 > maxDepth)? z3 : maxDepth;

		    double u = ipr.ui;
		    double v = ipr.vi;
		    
		    // intersection point between direction of OP and the Plane formed by [P1, P2, P3]
		    ipr.s = (x1*y2*z3 - x1*y3*z2 - x2*y1*z3 + x2*y3*z1 + x3*y1*z2 - x3*y2*z1) 
			/ (x1*y2 - x2*y1 - x1*y3 + x3*y1 + x2*y3 - x3*y2 + u*y1*z2 - u*y2*z1
			- v*x1*z2 + v*x2*z1 - u*y1*z3 + u*y3*z1 + v*x1*z3 - v*x3*z1 + u*y2*z3 
			- u*y3*z2 - v*x2*z3 + v*x3*z2);
		    ipr.v = ip_M::DEPTH_MES; // meaning this point has a depth measurement 
		    
		    // check the validity of the depth measurement 
		    if(maxDepth - minDepth > 2) // lie on an edge or noisy point? 
		    {	
			ipr.s = 0; 
			ipr.v = ip_M::NO_DEPTH; 
		    }else if(ipr.s - maxDepth > 0.2)
		    {
			ipr.s = maxDepth; 
		    }else if(ipr.s - minDepth < - 0.2)
		    {
			ipr.s = minDepth; 
		    }
		}else{
		    ipr.s = 0; 
		    ipr.v = ip_M::NO_DEPTH; 
		}
	    }else{
		ipr.s = 0; 
		ipr.v = ip_M::NO_DEPTH;
	    }
		
	    // if no depth 
	    if(ipr.v == ip_M::NO_DEPTH)
	    {
		// triangulation 
		// verify enough distance 
		tf::Transform first_pose = mFtTrans[i]; // i corresponds to feature i in ImgPTLast 
		tf::Vector3 dis_ = mLastPose.getOrigin() - first_pose.getOrigin(); 
		tf::Transform T21 = mLastPose.inverse()*first_pose; 
		tf::Transform I; 
		if(dis_.length() >= mdisThresholdForTriangulation) 
		{
		    Eigen::Vector2d p1(mFtObs->points[i].u, mFtObs->points[i].v); 
		    Eigen::Vector2d p2(ipr.ui, ipr.vi); 
		    Eigen::Vector3d pt_2 = triangulate_point(T21, I, p1, p2); 

		    double depth = pt_j(2); 
		    if(depth > 0.5 && depth < 50)
		    {
			ipr.s = depth; 
			ipr.v = ip_M::DEPTH_TRI; 
		    }
		}

		// if triangulation 
		if(ipr.v == ip_M::DEPTH_TRI)
		{
		    // the feature in lastImg do not has depth but already has depth before
		    // and also being triangulated, update depth 
		    if((*mvDptLast)[i] > 0)
		    {
			ipr.s = 3 * ipr.s *((*mvDptLast)[i])/(ipr.s + 2*((*mvDptLast)[i])); // 
		    }
		    (*mvDptLast)[i] = ipr.s; 
		}else if((*mvDptLast)[i] > 0)
		{
		    // if failed to triangulate, but already has depth 
		    ipr.s = (*mvDptLast)[i];
		    ipr.v = ip_M::DEPTH_TRI;
		}
	    }
	    ipr.ind = mImgPTLast->points[i].ind; 
	    ipRelations.push_back(ipr); 
	    // ipInd.push_back(mImgPTLast->points[i].ind); 
	}
    }
    
    // solve VO 
    Eigen::Vector7d vo_p; 
    vo_p << 0, 0, 0, 0, 0, 0, 1;
    int iter = 0; 
    int maxIterNum = 150; 
    
    // pcl::PointCloud<pcl::PointXYZHSV>::Ptr ipRelations2(new pcl::PointCloud<pcl::PointXYZHSV>());
    // vector<ip_M> ipRelations2; 
    // ipRelations2.reserve(ipRelations.size()); 
    // std::vector<float> ipy2;
    vector<Jacob_Q> vjq; 
    vjq.reserve(ipRelations.size()); 
    double scale_y2 = 100; 


    int ptNumNoDepthRec = 0;
    int ptNumWithDepthRec = 0;
    double meanValueWithDepthRec = 100000;

    for(iter = 0; iter < maxIterNum; iter++)
    {
	ipRelations2.clear(); 
	ipy2.clear(); 
	vjq.clear();
	int ptNumNoDepth = 0; 
	int ptNumWithDepth = 0; 
	double meanValWithDepth = 0;  

	// construct the problem
	for(int i=0; i<ipRelations.size(); i++)
	{
	    ipr = ipRelations[i]; 
	    Eigen::Vector3d pi(ipr.ui, ipr.vi, ipr.s); 
	    Eigen::Vector3d pj(ipr.uj, ipr.vj, 1.); 
	    Eigen::Matrix<double, 7, 1> J; 
	    if(ipr.v == ip_M::NO_DEPTH)
	    {	// pts without depth 
		double ey2 = Fy2(vo_p, pi, pj, &J); 
		
		if(ptNumNoDepthRec < 50 || iterNum < 25 || fabs(y2) < 2* meanValWithDepthRec/10000)
		{	
		    Jacob_Q jq; 
		    for(int j=0; j<7; j++)
		    {
			jq.de[j] = scale_y2 * J(j);
		    }
		    jq.err = scale_y2 * ey2;

		    vjq.push_back(jq);
		    ptNumNoDepth++; 
		}else
		{
		    ipRelations[i].v = ip_M::INVALID;     
		}
	    }else if(ipr.v == ip_M::DEPTH_MES || ipr.v == ip_M::DEPTH_TRI)
	    {
		// pts with depth 
		Eigen::Matrix<double, 7, 1> J1;
		double ey3 = Fy3(vo_p, pi, pj, &J); 
		double ey4 = Fy4(vo_p, pi, pj, &J1); 
		double val = sqrt(ey3 * ey3 + ey4 * ey4) ;
		if(ptNumWithDepthRec < 50 || iterNum < 25 || 
		    val < 2 * meanValWithDepthRec)
		{
		    Jacob_Q jq; 
		    for(int j=0; j<7; j++)
		    {
			jq.de[j] = J(j); 
		    }

		    jq.err = ey3;
		    vjq.push_back(jq);

		    for(int j=0; j<7; j++)
		    {
			jq.de[j] = J1(j); 
		    }
		    jq.err = ey4;
		    vjq.push_back(jq); 
		    meanValWithDepth += val; 
		    ptNumWithDepth++; 
		}
	    }
	}
	
	meanValWithDepth /= (ptNumWithDepth+0.001); 
	
	ptNumNoDepthRec = ptNumNoDepth;
	ptNumWithDepthRec = ptNumWithDepth; 
	meanValWithDepthRec = meanValWithDepth; 

	// solve the problem 
	int N_JE = vjq.size(); 
	if(N_JE > 10)
	{
	    cv::Mat matA(N_JE, 7, CV_32F, cv::Scalar::all(0));
	    cv::Mat matAt(7, N_JE, CV_32F, cv::Scalar::all(0));
	    cv::Mat matAtA(7, 7, CV_32F, cv::Scalar::all(0));
	    cv::Mat matB(N_JE, 1, CV_32F, cv::Scalar::all(0));
	    cv::Mat matAtB(7, 1, CV_32F, cv::Scalar::all(0));
	    cv::Mat matX(7, 1, CV_32F, cv::Scalar::all(0));
	    for(int i=0; i<N_JE; i++)
	    {
		Jacob_Q& jq = vjq[i]; 
		for(int j=0; j<7; j++)
		    matA.at<float>(i, j) = jq.de[j]; 
		matB.at<float>(i, 0) = -0.1*jq.err; 
	    }
	    cv::transpose(matA, matAt); 
	    matAtA = matAt*matA; 
	    matAtB = matAt * matB; 
	    cv::solve(matAtA, matAtB, matX, cv::DECOMP_QR); 
	    Eigen::Vector6d dmat; 
	    for(int j=0; j<3; j++)
	    {
		vo_p(j) += matX.at<float>(j, 0); 
		dmat(j) = matX.at<float>(j, 0); 
	    }
	    dmat(3) = matX.at<float>(3, 0); 
	    dmat(4) = matX.at<float>(4, 0); 
	    dmat(5) = matX.at<float>(5, 0); 
	    Eigen::Vector3d theta(dmat(3), dmat(4), dmat(5)); 
	    Eigen::Quaterniond dq = Utility::deltaQ(theta);
	    Eigen::Quaterniond q = (q*dq).normalized(); 
	    vo_p(3) = q.x(); vo_p(4) = q.y(); vo_p(5) = q.z(); vo_p(6) = q.w(); 

	    if(dmat.norm() < 0.00001)
		break;
	}
    }   

    // add vo to currentpose
    
    
}

void VisualOdometry::depthCloudHandler(const sensor_msgs::PointCloud2ConstPtr& depthCloud2)
{
    mPCTime = depthCloud2->header.stamp.toSec(); 
    
    mPC->clear(); 
    pcl::fromROSMsg(*depthCloud2, *mPC); 
    int depthCloudNum = mPC->points.size(); 
    if(depthCloudNum > 20)
    {
	for(int i=0; i<depthCloudNum; i++)
	{
	    mPC->points[i].x = mPC->points[i].x * mZoomDis / mPC->points[i].z;
	    mPC->points[i].y = mPC->points[i].y * mZoomDis / mPC->points[i].z;
	    mPC->points[i].intensity = mPC->points[i].z; 
	    mPC->points[i].z = mZoomDis;
	} 
	mKDTree->setInputCloud(mPC); 
    }
}


double VisualOdometry::Fy4(Eigen::Matrix<double, 7, 1>& pose, Eigen::Vector3d& pts_i, Eigen::Vector3d& pts_j, Eigen::Matrix<double, 7, 1>* J )
{
    double tx = pose[0];  // x
    double ty = pose[1];  // y
    double tz = pose[2];  // z
    double qx = pose[3];  // qx
    double qy = pose[4];  // qy
    double qz = pose[5];  // qz
    double qw = pose[6];  // qw
    
    // Eigen::Quaterniond seq qw, qx, qy, qz
    Eigen::Quaterniond q(qw, qx, qy, qz); 
    Eigen::Matrix<double, 3, 3> R = q.toRotationMatrix(); 

    double u0 = pts_i(0); double v0 = pts_i(1); double d0 = pts_i(2);  
    double u1 = pts_j(0); double v1 = pts_j(1); 
    double tmp1 = -tz * v1 + ty; 
    double tmp2 =  u1 * tz - tx;
    double tmp3 = -u1 * ty + v1 * tx;

    Eigen::Vector3d X0(u0*d0, v0*d0, d0); 
    Eigen::Matrix<double, 1, 3> tmp = R.row(1) - v1*R.row(2);
    double y4 = tmp * X0 + ty - v1*tz; 
    if(J)
    {
	(*J)(0) = 0; 
	(*J)(1) = 1; 
	(*J)(2) = -v1; 
	// dy4_dq
	Eigen::Matrix<double, 1, 3> dy_dq = -R.row(1)*Utility::skewSymmetric(X0) + v1 * R.row(2) * Utility::skewSymmetric(X0);
	(*J)(3) = dy_dq(0); 
	(*J)(4) = dy_dq(1); 
	(*J)(5) = dy_dq(2);
	(*J)(6) = 0.;
    }
    return y4;
}


double VisualOdometry::Fy3(Eigen::Matrix<double, 7, 1>& pose, Eigen::Vector3d& pts_i, Eigen::Vector3d& pts_j, Eigen::Matrix<double, 7, 1>* J )
{
    double tx = pose[0];  // x
    double ty = pose[1];  // y
    double tz = pose[2];  // z
    double qx = pose[3];  // qx
    double qy = pose[4];  // qy
    double qz = pose[5];  // qz
    double qw = pose[6];  // qw
    
    // Eigen::Quaterniond seq qw, qx, qy, qz
    Eigen::Quaterniond q(qw, qx, qy, qz); 
    Eigen::Matrix<double, 3, 3> R = q.toRotationMatrix(); 

    double u0 = pts_i(0); double v0 = pts_i(1); double d0 = pts_i(2); 
    double u1 = pts_j(0); double v1 = pts_j(1); 
    double tmp1 = -tz * v1 + ty; 
    double tmp2 =  u1 * tz - tx;
    double tmp3 = -u1 * ty + v1 * tx;

    Eigen::Vector3d X0(u0*d0, v0*d0, d0); 
    Eigen::Matrix<double, 1, 3> tmp = R.row(0) - u1*R.row(2); 
      
    double y3 = tmp * X0 + tx - u1*tz; 
    if(J)
    {
	(*J)(0) = 1; 
	(*J)(1) = 0; 
	(*J)(2) = -u1; 
	// dy3_dq
	Eigen::Matrix<double, 1, 3> dy_dq = -R.row(0)*Utility::skewSymmetric(X0) + u1*R.row(2)*Utility::skewSymmetric(X0);
	(*J)(3) = dy_dq(0); 
	(*J)(4) = dy_dq(1); 
	(*J)(5) = dy_dq(2); 
	(*J)(6) = 0.;
    }
    return y3;
}

double VisualOdometry::Fy2(Eigen::Matrix<double, 7, 1>& pose, Eigen::Vector3d& pts_i, Eigen::Vector3d& pts_j, Eigen::Matrix<double, 7, 1>* J)
{
    double tx = pose[0];  // x
    double ty = pose[1];  // y
    double tz = pose[2];  // z
    double qx = pose[3];  // qx
    double qy = pose[4];  // qy
    double qz = pose[5];  // qz
    double qw = pose[6];  // qw
    
    // Eigen::Quaterniond seq qw, qx, qy, qz
    Eigen::Quaterniond q(qw, qx, qy, qz); 
    Eigen::Matrix<double, 3, 3> R = q.toRotationMatrix(); 

    double u0 = pts_i(0); double v0 = pts_i(1); 
    double u1 = pts_j(0); double v1 = pts_j(1); 
    double tmp1 = -tz * v1 + ty; 
    double tmp2 =  u1 * tz - tx;
    double tmp3 = -u1 * ty + v1 * tx;

    Eigen::Vector3d X0(u0, v0, 1.); 
    Eigen::Vector3d tmp0 = R * X0; 
    double y2 = tmp1* tmp0(0) + tmp2*tmp0(1) + tmp3*tmp0(2); 
    
    if(J)
    {
	(*J)(0) = -tmp0(1) + v1*tmp0(2); 
	(*J)(1) = tmp0(0) - u1*tmp0(2);
	(*J)(2) = -v1*tmp0(0) + u1*tmp0(1); 
	
	// dy2_dq
	Eigen::Vector3d dy_dq; 
	Eigen::Vector3d tp1(tmp1, tmp2, tmp3); 
	Eigen::Matrix3d dtmp0_dq = R * -Utility::skewSymmetric(X0); 
	dy_dq = tp1.transpose() * dtmp0_dq;
	(*J)(3) = dy_dq(0); 
	(*J)(4) = dy_dq(1); 
	(*J)(5) = dy_dq(2);
	(*J)(6) = 0.; 
    }
    return y2;
}



