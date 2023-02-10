#include <edge_detection/edge_detection.h>

#include <opencv2/core/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <grid_map_cv/GridMapCvConverter.hpp>

#include <numeric>

#include <ros/ros.h>

namespace edge_detection {

    EdgeDetection::EdgeDetection (ros::NodeHandle& node_handle, std::string & frame_name, double & min_length, double & min_height):
    node_handle_(node_handle),
    min_length_(min_length),
    min_height_(min_height)
    {

      max_height_ = 10.0;
      max_length_ = 1.5;
      frame_name_ = frame_name;
      std::cout<<"[EdgeDetection::detectEdges] Parameters: "<<std::endl;
      std::cout<<"[EdgeDetection::detectEdges] min length: "<<min_length_<<std::endl;
      std::cout<<"[EdgeDetection::detectEdges] max length: "<<max_length_<<std::endl;
      std::cout<<"[EdgeDetection::detectEdges] min height: "<<min_height_<<std::endl;
      std::cout<<"[EdgeDetection::detectEdges] max height: "<<max_height_<<std::endl;

    }

    EdgeDetection::~EdgeDetection()
    {
    }

    bool EdgeDetection::advance(const Eigen::Vector3d & base_pose, const grid_map_msgs::GridMap & message){
      grid_map::GridMapRosConverter::fromMessage(message, gridMap_);

      cv::Mat image, im_edges, image_filtered;
      grid_map::GridMapCvConverter::toImage<unsigned char, 1>(
              gridMap_, "elevation", CV_8UC1, image);

      // Check if image is loaded fine
      if(image.empty()){
        printf(" Error opening image\n");
      }

      cv::medianBlur(image, image_filtered, 11);
      // Edge detection
      cv::Canny(image_filtered, im_edges, 50, 20, 3);
      // Standard Hough Line Transform
      std::vector<cv::Vec4i> lines; // will hold the results of the detection
      //cv::HoughLines(im_edges, lines, 1, CV_PI/180, 150, 0, 0 ); // runs the actual detection

      // Probabilistic Line Transform
      HoughLinesP(im_edges, lines, 1, CV_PI/180, 5, 5, 5 ); // runs the actual detection

      imwrite("image.png",image);
      imwrite("edge_edges.png",im_edges);
      imwrite("edge_filtered.png",image_filtered);

      base_pose_ = base_pose;

      checkExistingEdges(base_pose);

      findNewEdges(lines, base_pose);
      //setFakeEdges();

      findNextEdge();

      std::cout<<"[EdgeDetection::advance] number of detected edges: "<<edges_.size()<<std::endl;

      if(edges_.size()!=0){
        EdgeContainer next_edge = edges_.at(closest_orthogonal_edge_index_);
        edge_direction_ = next_edge.line_coeffs;
      }

      return true;
    }

    void EdgeDetection::setFakeEdges(const Eigen::Vector3d & base_pose){

      base_pose_ = base_pose;
      edges_.clear();

      Eigen::MatrixXd fake_points_wf(10,4);
      fake_points_wf << 0.74, 0.5, 0.74, -0.5,
      1.26, 0.5, 1.26, -0.5,
      1.88, 0.5, 1.88, -0.5,
      2.12, 0.5, 2.12, -0.5,
      3.0, 0.5, 3.0, -0.5,
      4.0, 0.5, 4.0, -0.5,
      4.74, 0.5, 4.74, -0.5,
      5.26, 0.5, 5.26, -0.5,
      6.38, 0.5, 6.38, -0.5,
      6.62, 0.5, 6.62, -0.5;

      Eigen::VectorXd fake_height(10);
      fake_height << 0.06, 0.06, 0.12, 0.12, 0.25, 0.25, 0.18, 0.18, 0.12, 0.12;
            
      for( size_t i = 0; i < 10; i++ ) {
        EdgeContainer new_edge;
        new_edge.point1_wf[0] = fake_points_wf(i,0);
        new_edge.point1_wf[1] = fake_points_wf(i,1);
        new_edge.point2_wf[0] = fake_points_wf(i,2);
        new_edge.point2_wf[1] = fake_points_wf(i,3);
        new_edge.length = 1.0;
        new_edge.height = fake_height[i];
        new_edge.z = fake_height[i];
        new_edge.yaw = 1.57;
        new_edge.line_coeffs = Eigen::Vector2d(sin(new_edge.yaw), cos(new_edge.yaw));

        if (!isEdgeRedundant(new_edge.point1_wf, new_edge.point2_wf)) {
          edges_.push_back(new_edge);
          orthogonal_edge_indices_.push_back(i);
        }
      }
    }

    void EdgeDetection::checkExistingEdges(const Eigen::Vector3d & base_pose){
      if(edges_.size()>0){
        std::vector<edge_detection::EdgeContainer> edges_tmp = edges_;
        edges_.clear();
        for( size_t i = 0; i < edges_tmp.size(); i++ ){
          double height_check = computeStepHeight(edges_tmp.at(i).point1_wf, edges_tmp.at(i).point2_wf, edges_tmp.at(i).z);
          if((fabs(height_check) > min_height_)&&(fabs(height_check)< max_height_)){
            double robot_yaw = base_pose[2];
              edges_.push_back(edges_tmp.at(i));
          }
        }

        sortEdgesFromClosestToFurthest(base_pose);
      }
    }

    void EdgeDetection::sortEdgesFromClosestToFurthest(const Eigen::Vector3d & base_pose){
      if(edges_.size()>0){

        std::vector<edge_detection::EdgeContainer> edges_tmp = edges_;
        Eigen::VectorXd distances(edges_.size());
        Eigen::Vector2d base_pos = Eigen::Vector2d(base_pose[0], base_pose[1]);
        for( size_t i = 0; i < edges_.size(); i++ ){
          distances[i] = computeSignedDistanceBtwEdgeAndBaseInWorldFrame(edges_.at(i).point1_wf, edges_.at(i).point2_wf, base_pos);
        }

        std::vector<int> V(edges_.size());
        int x=0;
        std::iota(V.begin(),V.end(),x++); //Initializing
        std::sort( V.begin(),V.end(), [&](int i,int j){return distances[i]<distances[j];} );

        edges_.clear();
        for( size_t i = 0; i < edges_tmp.size(); i++ ){
          edges_.push_back(edges_tmp.at(V.at(i)));
        }

      }
    }

    void EdgeDetection::findNewEdges(const std::vector<cv::Vec4i> & lines, const Eigen::Vector3d & base_pose_so2){
      orthogonal_edge_indices_.clear();
      double robot_yaw_angle = base_pose_(2);
      Eigen::Vector2d robot_pos = Eigen::Vector2d(base_pose_so2[0], base_pose_so2[1]);

      for( size_t i = 0; i < lines.size(); i++ ) {
        cv::Vec4i l = lines[i];
        EdgeContainer new_edge;
        new_edge.point1_wf = convertImageToOdomFrame(gridMap_.getResolution(), gridMap_.getSize(), l[0], l[1]);
        new_edge.point2_wf = convertImageToOdomFrame(gridMap_.getResolution(), gridMap_.getSize(), l[2], l[3]);
        clockwiseSort(new_edge);
        new_edge.length = computeLength(new_edge.point1_wf, new_edge.point2_wf);
        if ((new_edge.length > min_length_)&&(new_edge.length < max_length_)) {
          double edge_yaw_wf = computeEdgeOrientation(new_edge.point1_wf, new_edge.point2_wf);
          new_edge.line_coeffs = Eigen::Vector2d(sin(edge_yaw_wf), cos(edge_yaw_wf));
          Eigen::Vector2d target_pos = robot_pos + 1.0*Eigen::Vector2d(cos(robot_yaw_angle), sin(robot_yaw_angle));
          Eigen::Vector2d edge_pos = (new_edge.point1_wf+new_edge.point2_wf)/2.0;
          //if(isInsideEllipse(robot_yaw_angle, robot_pos, edge_pos, 1.0, 2.0)){
            new_edge.height = computeStepHeight(new_edge.point1_wf, new_edge.point2_wf, new_edge.z);
            if ((fabs(new_edge.height) > min_height_)&&(fabs(new_edge.height) < max_height_)){
                if (!isEdgeRedundant(new_edge.point1_wf, new_edge.point2_wf)) {
                  new_edge.yaw = edge_yaw_wf;
                  new_edge.line_coeffs = Eigen::Vector2d(sin(new_edge.yaw), cos(new_edge.yaw));;
                  edges_.push_back(new_edge);
                  orthogonal_edge_indices_.push_back(i);
                }
            }
          //}

        }
      }

    }

    edge_idx EdgeDetection::findNextEdge(){
      double min_dist = 10000.0;
      edge_idx closest_idx = 0;
      for( size_t i = 0; i < edges_.size(); i++ ){
        Eigen::Vector2d base_pos = Eigen::Vector2d(base_pose_[0], base_pose_[1]);
        double distance_from_base = computeDistanceBtwEdgeAndBaseInWorldFrame(edges_.at(i).point1_wf, edges_.at(i).point2_wf, base_pos);
        if(distance_from_base<min_dist){
          min_dist = distance_from_base;
          closest_idx = i;
        }
      }
      closest_orthogonal_edge_index_ = closest_idx;
      return closest_idx;
    }

    Eigen::Vector2d EdgeDetection::convertImageToOdomFrame(const double & resolution, const Eigen::Array2i & grid_size, const int & p_x, const int & p_y){

      double gsx = grid_size[0];
      double gsy = grid_size[1];
      double p_x_base = (- p_y + gsy/2.0)*resolution;
      double p_y_base = (- p_x + gsx/2.0)*resolution;
      Eigen::Vector2d pos_bf = Eigen::Vector2d(p_x_base, p_y_base);
      Eigen::Vector2d pos_odom_frame = pos_bf+gridMap_.getPosition();
      return pos_odom_frame;
    }

    double EdgeDetection::GetHeight(double & x, double & y) {
      grid_map::Position pos = {x,y};
      if (!gridMap_.isInside(pos)){
        return 1e9;
      }
      else {
        double height = static_cast<double>(gridMap_.atPosition("elevation", pos, grid_map::InterpolationMethods::INTER_NEAREST));
        if (std::isnan(height)) {
          return 1e9;
        }
        return height;
      }
    }

    double EdgeDetection::computeLength(const Eigen::Vector2d & p1, const Eigen::Vector2d & p2){
      return sqrt(pow(p1[0] - p2[0],2) + pow(p1[1] - p2[1],2));
    }

    bool EdgeDetection::isInsideEllipse(const double & edge_yaw,
            const Eigen::Vector2d & ellipse_center,
            const Eigen::Vector2d & p,
            const double & d1,
            const double & d2){
      Eigen::AngleAxisd rollAngle(0.0, Eigen::Vector3d::UnitX());
      Eigen::AngleAxisd pitchAngle(0.0, Eigen::Vector3d::UnitY());
      Eigen::AngleAxisd yawAngle(-edge_yaw + M_PI/2.0, Eigen::Vector3d::UnitZ());
      Eigen::Quaternion<double> q = yawAngle * pitchAngle * rollAngle;
      Eigen::Matrix3d rotationMatrix = q.matrix();
      Eigen::Vector2d point2d = p - ellipse_center;
      Eigen::Vector2d point = (rotationMatrix*Eigen::Vector3d(point2d[0], point2d[1], 0.0)).segment(0,2);
      double a2 = pow(d1,2);
      double b2 = pow(d2,2);
      double y = pow(point[0],2)/a2 + pow(point[1],2)/b2;
      if (y<=1.0){
        return true;
      }else{
        return false;
      }
    }

    bool EdgeDetection::hasSimilarLineCoefficients(const EdgeContainer & existing_edge,
            const Eigen::Vector2d & p1,
            const Eigen::Vector2d & p2,
            const Eigen::Vector2d & base_pos){
      double new_adge_yaw = computeEdgeOrientation(p1, p2);
      Eigen::Vector2d new_line_coeffs = Eigen::Vector2d(sin(new_adge_yaw), cos(new_adge_yaw));
      double existing_edge_distance = computeDistanceBtwEdgeAndBaseInWorldFrame(existing_edge.point1_wf, existing_edge.point2_wf, base_pos);
      double new_distance = computeDistanceBtwEdgeAndBaseInWorldFrame(p1, p2, base_pos);
      if(fabs(existing_edge.line_coeffs(0)-new_line_coeffs(0))<10){
        if(fabs(existing_edge.line_coeffs(1)-new_line_coeffs(1))<10){
          if(fabs(existing_edge_distance - new_distance)< 0.4){
            return true;
          }
        }
      }
      return false;
    }

    double EdgeDetection::computeEdgeOrientation(const Eigen::Vector2d & p1, const Eigen::Vector2d & p2){
      double yaw_angle = atan2(p1[1] - p2[1], p1[0] - p2[0]);
      return yaw_angle;
    }

    double EdgeDetection::computeStepHeight(const Eigen::Vector2d & p1_wf, const Eigen::Vector2d & p2_wf, double & z_coordinate){

      double edge_yaw = computeEdgeOrientation(p1_wf, p2_wf);
      Eigen::Vector2d edge_normal = Eigen::Vector2d(sin(edge_yaw), cos(edge_yaw));

      Eigen::Vector2d robot_direction = Eigen::Vector2d(cos(base_pose_(2)), sin(base_pose_(2)));
      double project_robot_direction_on_edge_normal = robot_direction.dot(edge_normal);
      if(project_robot_direction_on_edge_normal<0){ //make sure that the edge normal is aligned with the robot to make code more robust against occlusions
        edge_normal = -edge_normal;
      }

      Eigen::VectorXd heights(10);

      for(int i = 0; i<10; i++){
        double idx = 0.1*(double)i + 0.05;
        Eigen::Vector2d p = p1_wf + (p2_wf - p1_wf)*idx;
        heights(i) = computeHeight(edge_normal, p, z_coordinate);;
      }

      double height = heights.sum()/heights.size();
      return height;
    }

    double EdgeDetection::computeHeight(const Eigen::Vector2d & edge_normal,
            const Eigen::Vector2d & point2check,
            double & z_coordinate){
      double epsilon_plus = 0.15; //x_distribution_plus(rng);
      double epsilon_minus = 0.15; //x_distribution_minus(rng);
      Eigen::Vector2d middle_point_wf_plus = point2check + edge_normal*epsilon_plus;
      Eigen::Vector2d middle_point_wf_minus = point2check - edge_normal*epsilon_minus;
      double z1 = GetHeight(middle_point_wf_plus[0], middle_point_wf_plus[1]);
      double z2 = GetHeight(middle_point_wf_minus[0], middle_point_wf_minus[1]);
      z_coordinate = std::max(z1, z2);
      return z1 - z2;
    }

    double EdgeDetection::computeDistance(const Eigen::Vector2d & p1, const Eigen::Vector2d & p2){
      return computeDistanceBtwEdgeAndBaseInWorldFrame(p1, p2, Eigen::Vector2d(0.0, 0.0));
    }

    double EdgeDetection::computeDistanceBtwEdgeAndBaseInWorldFrame(const Eigen::Vector2d & p1_wf,
            const Eigen::Vector2d & p2_wf,
            const Eigen::Vector2d & base_pos){

      return fabs(computeSignedDistanceBtwEdgeAndBaseInWorldFrame(p1_wf, p2_wf, base_pos));
    }

    double EdgeDetection::computeSignedDistanceBtwEdgeAndBaseInWorldFrame(const Eigen::Vector2d & p1_wf,
                                                                    const Eigen::Vector2d & p2_wf,
                                                                    const Eigen::Vector2d & base_pos){

      double num = (p2_wf[1] - p1_wf[1])*base_pos[0] - (p2_wf[0] - p1_wf[0])*base_pos[1] + p2_wf[0]*p1_wf[1] - p2_wf[1]*p1_wf[0];
      double denum = computeLength(p1_wf, p2_wf);
      return num/denum;
    }

    bool EdgeDetection::isEdgeRedundant(const Eigen::Vector2d & p1_wf, const Eigen::Vector2d & p2_wf){

      bool merge_redundant_edges = true;

      for( size_t i = 0; i < edges_.size(); i++ )
      {
        bool d11 = isInsideEllipse(edges_.at(i).yaw, edges_.at(i).point1_wf, p1_wf, 0.2, min_length_);
        bool d12 = isInsideEllipse(edges_.at(i).yaw, edges_.at(i).point2_wf, p1_wf, 0.2, min_length_);
        bool d21 = isInsideEllipse(edges_.at(i).yaw, edges_.at(i).point1_wf, p2_wf, 0.2, min_length_);
        bool d22 = isInsideEllipse(edges_.at(i).yaw, edges_.at(i).point2_wf, p2_wf, 0.2, min_length_);

        Eigen::Vector2d base_pos = base_pose_.segment(0,2);


        if(hasSimilarLineCoefficients(edges_.at(i), p1_wf, p2_wf, base_pos)){
          if(merge_redundant_edges){
            if(d11&&d22){
            edges_.at(i).point1_wf = (edges_.at(i).point1_wf + p1_wf)/2.0;
            edges_.at(i).point2_wf = (edges_.at(i).point2_wf + p2_wf)/2.0;
            }
            edges_.at(i).length = computeLength(edges_.at(i).point1_wf, edges_.at(i).point2_wf);
            edges_.at(i).yaw = computeEdgeOrientation(edges_.at(i).point1_wf, edges_.at(i).point2_wf);
            edges_.at(i).line_coeffs = Eigen::Vector2d(sin(edges_.at(i).yaw), cos(edges_.at(i).yaw));
            edges_.at(i).height = computeStepHeight(edges_.at(i).point1_wf, edges_.at(i).point2_wf, edges_.at(i).z);
          }
          return true;
        }
      }
      std::cout<<"[EdgeDetection::isEdgeRedundant] edge is not redundant!"<<std::endl;
      return false;
    }

    double EdgeDetection::limitAngle(const double & yaw){
      double new_yaw;
      if (yaw>M_PI/2.0){
        new_yaw = yaw - M_PI;
      }else if(yaw<M_PI/2.0){
        new_yaw = yaw + M_PI;
      }else{
        new_yaw= yaw;
      }
      
      return yaw;
    }

    bool EdgeDetection::isEdgeFacingRobot(const double & edge_yaw_wf, const double & robot_yaw_angle){
    double delta_range = M_PI / 6.0;
    double wrapped_yaw_angle = limitAngle(edge_yaw_wf - M_PI/2.0);
    double wrapped_robot_angle = limitAngle(robot_yaw_angle);
    if(fabs(wrapped_yaw_angle - wrapped_robot_angle) <  delta_range) {
      return true;
    }else{
      return false;
    }
      
    }

    double EdgeDetection::getEdgeDistanceFromBase(const Eigen::Vector3d & base_pose){
      Eigen::Vector2d base_pos = Eigen::Vector2d(base_pose[0], base_pose[1]);
      double distance_from_base = computeDistanceBtwEdgeAndBaseInWorldFrame(edges_.at(closest_orthogonal_edge_index_).point1_wf,
              edges_.at(closest_orthogonal_edge_index_).point2_wf,
              base_pos);
      return distance_from_base;
    }

    void EdgeDetection::clockwiseSort(EdgeContainer & edge){

      Eigen::Vector2d p1 = edge.point1_wf;
      Eigen::Vector2d p2 = edge.point2_wf;
      if(p1(0) > p2(0)){
        edge.point1_wf = p1;
        edge.point2_wf = p2;
      } else {
        edge.point1_wf = p2;
        edge.point2_wf = p1;
      }

    }

    Eigen::Vector2d EdgeDetection::getPointAlongEdgeInWorldFrame(edge_idx idx){
      Eigen::Vector2d middle_point_wf = (edges_.at(idx).point1_wf + edges_.at(idx).point2_wf)/2.0;
      return middle_point_wf;
    }

    Eigen::Vector2d EdgeDetection::getEdgeDirectionInBaseFrame(){
      return edge_direction_;
    }

    double EdgeDetection::getNextStepHeight(){
      return edges_.at(closest_orthogonal_edge_index_).height;
    }

    double EdgeDetection::getStepHeight(edge_idx idx){
      return edges_.at(idx).height;
    }

    double EdgeDetection::getEdgeYawAngleInWorldFrame(edge_idx idx){
      return computeEdgeOrientation(edges_.at(idx).point1_wf, edges_.at(idx).point2_wf);
    }

    int EdgeDetection::numberOfDetectedEdges(){
      return edges_.size();
    }
}
