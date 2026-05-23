#pragma once
#include <mutex>
#include <string>

struct LatestGazePointData
{
    long long system_time_ms = 0;
    long long device_time_us = 0;
    int validity = 0;
    float position_xy[2] = { 0.0f, 0.0f };

    double pixel_x = 0.0;
    double pixel_y = 0.0;
    int screen_w = 2560;
    int screen_h = 1440;

    int trigger_index = 0;
    int success_index = 0;
};

struct LatestGazeOriginData
{
    long long system_time_ms = 0;
    long long device_time_us = 0;

    int left_validity = 0;
    float left_xyz[3] = { 0.0f, 0.0f, 0.0f };

    int right_validity = 0;
    float right_xyz[3] = { 0.0f, 0.0f, 0.0f };
};

struct LatestEyePositionNormalizedData
{
    long long system_time_ms = 0;
    long long device_time_us = 0;

    int left_validity = 0;
    float left_xyz[3] = { 0.0f, 0.0f, 0.0f };

    int right_validity = 0;
    float right_xyz[3] = { 0.0f, 0.0f, 0.0f };
};

struct LatestUserPresenceData
{
    long long system_time_ms = 0;
    long long device_time_us = 0;
    int status = 0;
};

struct LatestHeadPoseData
{
    long long system_time_ms = 0;
    long long device_time_us = 0;

    int position_validity = 0;
    float position_xyz[3] = { 0.0f, 0.0f, 0.0f };

    int rotation_validity_xyz[3] = { 0, 0, 0 };
    float rotation_xyz[3] = { 0.0f, 0.0f, 0.0f };
};

struct LatestNotificationData
{
    long long system_time_ms = 0;

    int type = 0;
    int value_type = 0;

    float value_float = 0.0f;
    unsigned int value_uint = 0;
    int value_state = 0;
    int value_enabled_eye = 0;
    std::string value_string;
};

struct LatestUserPositionGuideData
{
    long long system_time_ms = 0;
    long long device_time_us = 0;

    int left_position_validity = 0;
    float left_position_normalized_xyz[3] = { 0.0f, 0.0f, 0.0f };

    int right_position_validity = 0;
    float right_position_normalized_xyz[3] = { 0.0f, 0.0f, 0.0f };
};

struct StreamSubscriptionState
{
    bool gaze_point = false;
    bool gaze_origin = false;
    bool eye_position_normalized = false;
    bool user_presence = false;
    bool head_pose = false;
    bool notifications = false;
    bool user_position_guide = false;
};

struct StreamRuntimeStats
{
    long long gaze_point_count = 0;
    long long gaze_origin_count = 0;
    long long eye_position_normalized_count = 0;
    long long user_presence_count = 0;
    long long head_pose_count = 0;
    long long notifications_count = 0;
    long long user_position_guide_count = 0;

    long long last_gaze_point_time_ms = 0;
    long long last_gaze_origin_time_ms = 0;
    long long last_eye_position_normalized_time_ms = 0;
    long long last_user_presence_time_ms = 0;
    long long last_head_pose_time_ms = 0;
    long long last_notifications_time_ms = 0;
    long long last_user_position_guide_time_ms = 0;
};

struct CaptureStats
{
    int trigger_index = 0;
    int success_index = 0;
    int success[1000] = { 0 };
    bool last_trigger_success = false;
};

struct CaptureContext
{
    bool experiment_recording = false;
    bool stage_recording = false;

    std::string experiment_id;
    std::string experiment_name;
    std::string experiment_file_path;
    long long experiment_start_time_ms = 0;
    long long experiment_stop_time_ms = 0;
    int experiment_event_count = 0;

    int stage_index = 0;
    std::string stage_id;
    std::string stage_name;
    std::string stage_file_path;
    long long stage_start_time_ms = 0;
    long long stage_stop_time_ms = 0;
    int stage_event_count = 0;
};

struct AppState
{
    std::mutex mtx;

    bool running = false;
    bool device_connected = false;
    bool recording = false;
    bool stop_requested = false;

    StreamSubscriptionState streams;
    StreamRuntimeStats runtime_stats;
    CaptureStats stats;
    CaptureContext capture;

    LatestGazePointData gaze_point;
    LatestGazeOriginData gaze_origin;
    LatestEyePositionNormalizedData eye_position_normalized;
    LatestUserPresenceData user_presence;
    LatestHeadPoseData head_pose;
    LatestNotificationData notification;
    LatestUserPositionGuideData user_position_guide;
};

extern AppState g_app_state;