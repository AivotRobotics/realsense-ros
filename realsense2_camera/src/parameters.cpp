#include "../include/base_realsense_node.h"
#include <ros_utils.h>
#include <iomanip>

using namespace realsense2_camera;

void BaseRealSenseNode::getParameters()
{
    ROS_INFO("getParameters...");
    _parameters.setParamT(std::string("align_depth"), rclcpp::ParameterValue(ALIGN_DEPTH), _align_depth);

    _publish_tf = _node.declare_parameter("publish_tf", rclcpp::ParameterValue(PUBLISH_TF)).get<rclcpp::PARAMETER_BOOL>();
    _tf_publish_rate = _node.declare_parameter("tf_publish_rate", rclcpp::ParameterValue(TF_PUBLISH_RATE)).get<rclcpp::PARAMETER_DOUBLE>();

    _parameters.setParamT(std::string("enable_sync"), rclcpp::ParameterValue(SYNC_FRAMES || _align_depth), _sync_frames);

    _json_file_path = _node.declare_parameter("json_file_path", rclcpp::ParameterValue("")).get<rclcpp::PARAMETER_STRING>();

    std::string unite_imu_method_str = _node.declare_parameter("unite_imu_method", rclcpp::ParameterValue(DEFAULT_UNITE_IMU_METHOD)).get<rclcpp::PARAMETER_STRING>();
    if (unite_imu_method_str == "linear_interpolation")
        _imu_sync_method = imu_sync_method::LINEAR_INTERPOLATION;
    else if (unite_imu_method_str == "copy")
        _imu_sync_method = imu_sync_method::COPY;
    else
        _imu_sync_method = imu_sync_method::NONE;

    _allow_no_texture_points = _node.declare_parameter("allow_no_texture_points", rclcpp::ParameterValue(ALLOW_NO_TEXTURE_POINTS)).get<rclcpp::PARAMETER_BOOL>();
    _clipping_distance = static_cast<float>(_node.declare_parameter("clip_distance", rclcpp::ParameterValue(-1.0)).get<rclcpp::PARAMETER_DOUBLE>());
    _linear_accel_cov = _node.declare_parameter("linear_accel_cov", rclcpp::ParameterValue(0.01)).get<rclcpp::PARAMETER_DOUBLE>();
    _angular_velocity_cov = _node.declare_parameter("angular_velocity_cov", rclcpp::ParameterValue(0.01)).get<rclcpp::PARAMETER_DOUBLE>();
    _hold_back_imu_for_frames = _node.declare_parameter("hold_back_imu_for_frames", rclcpp::ParameterValue(HOLD_BACK_IMU_FOR_FRAMES)).get<rclcpp::PARAMETER_BOOL>();
    _publish_odom_tf = _node.declare_parameter("publish_odom_tf", rclcpp::ParameterValue(PUBLISH_ODOM_TF)).get<rclcpp::PARAMETER_BOOL>();
}

bool is_checkbox(rs2::options sensor, rs2_option option)
{
    rs2::option_range op_range = sensor.get_option_range(option);
    return op_range.max == 1.0f &&
        op_range.min == 0.0f &&
        op_range.step == 1.0f;
}

bool is_enum_option(rs2::options sensor, rs2_option option)
{
    static const int MAX_ENUM_OPTION_VALUES(100);
    static const float EPSILON(0.05);
    
    rs2::option_range op_range = sensor.get_option_range(option);
    if (abs((op_range.step - 1)) > EPSILON || (op_range.max > MAX_ENUM_OPTION_VALUES)) return false;
    for (auto i = op_range.min; i <= op_range.max; i += op_range.step)
    {
        if (sensor.get_option_value_description(option, i) == nullptr)
            continue;
        return true;
    }
    return false;
}

bool is_int_option(rs2::options sensor, rs2_option option)
{
    rs2::option_range op_range = sensor.get_option_range(option);
    return (op_range.step == 1.0);
}

std::map<std::string, int> get_enum_method(rs2::options sensor, rs2_option option)
{
    std::map<std::string, int> dict; // An enum to set size
    if (is_enum_option(sensor, option))
    {
        rs2::option_range op_range = sensor.get_option_range(option);
        const auto op_range_min = int(op_range.min);
        const auto op_range_max = int(op_range.max);
        const auto op_range_step = int(op_range.step);
        for (auto val = op_range_min; val <= op_range_max; val += op_range_step)
        {
            if (sensor.get_option_value_description(option, val) == nullptr)
                continue;
            dict[sensor.get_option_value_description(option, val)] = val;
        }
    }
    return dict;
}

void BaseRealSenseNode::set_auto_exposure_roi(const std::string option_name, rs2::sensor sensor, int new_value)
{
    rs2::region_of_interest& auto_exposure_roi(_auto_exposure_roi[sensor.get_info(RS2_CAMERA_INFO_NAME)]);
    if (option_name == "left")
        auto_exposure_roi.min_x = new_value;
    else if (option_name == "right")
        auto_exposure_roi.max_x = new_value;
    else if (option_name == "top")
        auto_exposure_roi.min_y = new_value;
    else if (option_name == "bottom")
        auto_exposure_roi.max_y = new_value;
    else
    {
        ROS_WARN_STREAM("Invalid option_name: " << option_name << " while setting auto exposure ROI.");
        return;
    }
    set_sensor_auto_exposure_roi(sensor);
}

void BaseRealSenseNode::set_sensor_auto_exposure_roi(rs2::sensor sensor)
{
    const rs2::region_of_interest& auto_exposure_roi(_auto_exposure_roi[sensor.get_info(RS2_CAMERA_INFO_NAME)]);
    try
    {
        sensor.as<rs2::roi_sensor>().set_region_of_interest(auto_exposure_roi);
    }
    catch(const std::runtime_error& e)
    {
        ROS_ERROR_STREAM(e.what());
    }
}

#ifdef false
void BaseRealSenseNode::readAndSetDynamicParam(ros::NodeHandle& nh1, std::shared_ptr<ddynamic_reconfigure::DDynamicReconfigure> ddynrec, 
                                               const std::string option_name, const int min_val, const int max_val, rs2::sensor sensor, 
                                               int* option_value)
{
    nh1.param(option_name, *option_value, *option_value); //param (const std::string &param_name, T &param_val, const T &default_val) const
    if (*option_value < min_val) *option_value = min_val;
    if (*option_value > max_val) *option_value = max_val;
    
    ddynrec->registerVariable<int>(
        option_name, *option_value, [this, sensor, option_name](int new_value){set_auto_exposure_roi(option_name, sensor, new_value);},
        "auto-exposure " + option_name + " coordinate", min_val, max_val);
}

void BaseRealSenseNode::registerAutoExposureROIOptions(ros::NodeHandle& nh)
{
    for (const std::pair<stream_index_pair, std::vector<rs2::stream_profile>>& profile : _enabled_profiles)
    {
        rs2::sensor sensor = _sensors[profile.first];
        std::string module_base_name(sensor.get_info(RS2_CAMERA_INFO_NAME));
        if (sensor.is<rs2::roi_sensor>() && _auto_exposure_roi.find(module_base_name) == _auto_exposure_roi.end())
        {
            int max_x(_width[profile.first]-1);
            int max_y(_height[profile.first]-1);

            std::string module_name = create_graph_resource_name(module_base_name) +"/auto_exposure_roi";
            ros::NodeHandle nh1(nh, module_name);
            std::shared_ptr<ddynamic_reconfigure::DDynamicReconfigure> ddynrec = std::make_shared<ddynamic_reconfigure::DDynamicReconfigure>(nh1);

            _auto_exposure_roi[module_base_name] = {0, 0, max_x, max_y};
            rs2::region_of_interest& auto_exposure_roi(_auto_exposure_roi[module_base_name]);
            readAndSetDynamicParam(nh1, ddynrec, "left", 0, max_x, sensor, &(auto_exposure_roi.min_x));
            readAndSetDynamicParam(nh1, ddynrec, "right", 0, max_x, sensor, &(auto_exposure_roi.max_x));
            readAndSetDynamicParam(nh1, ddynrec, "top", 0, max_y, sensor, &(auto_exposure_roi.min_y));
            readAndSetDynamicParam(nh1, ddynrec, "bottom", 0, max_y, sensor, &(auto_exposure_roi.max_y));

            ddynrec->publishServicesTopics();
            _ddynrec.push_back(ddynrec);

            // Initiate the call to set_sensor_auto_exposure_roi, after the first frame arrive.
            rs2_stream stream_type = profile.first.first;
            _video_functions_stack[stream_type].push_back([this, sensor](){set_sensor_auto_exposure_roi(sensor);});
            _is_first_frame[stream_type] = true;
        }
    }
}
#endif //#ifdef false

template<class T>
void param_set_option(rs2::options sensor, rs2_option option, std::string option_name, const rclcpp::Parameter& parameter)
{ 
    std::cout << "set_option: " << option_name << " = " << parameter.get_value<T>() << std::endl;
    try
    {
        sensor.set_option(option, parameter.get_value<T>());
    }
    catch(const rs2::invalid_value_error& e)
    {
        std::cout << "Failed to set value: " << e.what() << std::endl;
    }
}

template<class T>
void BaseRealSenseNode::set_parameter(rs2::options sensor, rs2_option option, const std::string& module_name, const std::string& description_addition)
{
    const std::string option_name(module_name + "." + create_graph_resource_name(rs2_option_to_string(option)));
    T option_value;
    try
    {
        option_value = static_cast<T>(sensor.get_option(option));
    }
    catch(const rs2::invalid_value_error& e)
    {
        ROS_WARN_STREAM("Failed to get value for " << option_name << " : " << e.what());
        return;
    }
    rs2::option_range op_range = sensor.get_option_range(option);
    rcl_interfaces::msg::ParameterDescriptor crnt_descriptor;
    std::stringstream desc;
    desc << sensor.get_option_description(option) << std::endl << description_addition;
    crnt_descriptor.description = desc.str();
    if (std::is_same<T, int>::value || std::is_same<T, bool>::value)
    {
        rcl_interfaces::msg::IntegerRange range;
        range.from_value = int(op_range.min);
        range.to_value = int(op_range.max);
        crnt_descriptor.integer_range.push_back(range);
        if (std::is_same<T, bool>::value)
            ROS_DEBUG_STREAM("Declare: BOOL::" << option_name << " = " << option_value << "[" << op_range.min << ", " << op_range.max << "]");
        else
            ROS_DEBUG_STREAM("Declare: INT::" << option_name << " = " << option_value << "[" << op_range.min << ", " << op_range.max << "]");
    }
    else
    {
        rcl_interfaces::msg::FloatingPointRange range;
        range.from_value = double(op_range.min);
        range.to_value = double(op_range.max);
        crnt_descriptor.floating_point_range.push_back(range);
        ROS_DEBUG_STREAM("Declare: DOUBLE::" << option_name << " = " << option_value);
    }

    T new_val;
    try
    {
        rclcpp::ParameterValue pValue = (_parameters.setParam(option_name, rclcpp::ParameterValue(option_value), [option, sensor, option_name](const rclcpp::Parameter& parameter)
                    {
                        param_set_option<T>(sensor, option, option_name, parameter);
                    }, crnt_descriptor));
        new_val = pValue.get<T>();
    }
    catch(const rclcpp::exceptions::InvalidParameterValueException& e)
    {
        ROS_WARN_STREAM("Failed to set parameter:" << option_name << " = " << option_value << "[" << op_range.min << ", " << op_range.max << "]\n" << e.what());
        return;
    }
    
    if (new_val != option_value)
    {
        try
        {
            sensor.set_option(option, new_val);
        }
        catch(const rs2::invalid_value_error& e)
        {
            ROS_WARN_STREAM("Failed to set value to sensor: " << option_name << " = " << option_value << "[" << op_range.min << ", " << op_range.max << "]\n" << e.what());            
        }
    }
}

void BaseRealSenseNode::registerDynamicOptions(rs2::options sensor, const std::string& module_name)
{
    rclcpp::Parameter node_param;
    for (auto i = 0; i < RS2_OPTION_COUNT; i++)
    {
        rs2_option option = static_cast<rs2_option>(i);
        const std::string option_name(module_name + "." + create_graph_resource_name(rs2_option_to_string(option)));
        if (!sensor.supports(option) || sensor.is_option_read_only(option))
        {
            continue;
        }
        if (is_checkbox(sensor, option))
        {
            set_parameter<bool>(sensor, option, module_name);
            continue;
        }
        const auto enum_dict = get_enum_method(sensor, option);
        if (enum_dict.empty())
        {
            if (is_int_option(sensor, option))
            {
                set_parameter<int>(sensor, option, module_name);
            }
            else
            {
                if (i == RS2_OPTION_DEPTH_UNITS)
                {
                    rs2::option_range op_range = sensor.get_option_range(option);
                    if (ROS_DEPTH_SCALE >= op_range.min && ROS_DEPTH_SCALE <= op_range.max)
                    {
                        try
                        {
                            sensor.set_option(option, ROS_DEPTH_SCALE);
                        }
                        catch(const rs2::invalid_value_error& e)
                        {
                            std::cout << "Failed to set value: " << e.what() << std::endl;
                        }
                        op_range.min = ROS_DEPTH_SCALE;
                        op_range.max = ROS_DEPTH_SCALE;

                        _depth_scale_meters = ROS_DEPTH_SCALE;
                    }
                }
                else
                {
                    set_parameter<double>(sensor, option, module_name);
                }
            }
        }
        else
        {
            std::vector<std::pair<std::string, int> > enum_vec;
            size_t longest_desc(0);
            for (auto enum_iter : enum_dict)
            {
                enum_vec.push_back(std::make_pair(enum_iter.first, enum_iter.second));
                longest_desc = std::max(longest_desc, enum_iter.first.size());
            }
            sort(enum_vec.begin(), enum_vec.end(), [](std::pair<std::string, int> e1, std::pair<std::string, int> e2){return (e1.second < e2.second);});
            std::stringstream description;
            for (auto vec_iter : enum_vec)
            {
                description << std::setw(longest_desc+6) << std::left << vec_iter.first << " : " << vec_iter.second << std::endl;
            }
            set_parameter<int>(sensor, option, module_name, description.str());
        }
    }
}

void BaseRealSenseNode::registerDynamicParameters()
{
    ROS_INFO("Setting Dynamic parameters.");

    for(auto&& sensor : _available_ros_sensors)
    {
        std::string module_name = create_graph_resource_name(sensor->get_info(RS2_CAMERA_INFO_NAME));
        ROS_DEBUG_STREAM("module_name:" << module_name);
        sensor->registerProfileParameters();
        registerDynamicOptions(*sensor, module_name);
    }
    ROS_INFO("Done Setting Dynamic reconfig parameters.");
}

