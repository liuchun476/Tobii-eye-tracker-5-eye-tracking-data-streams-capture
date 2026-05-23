#ifndef _SAVE_DATA_H
#define _SAVE_DATA_H

#include "global.h"
#include "shared_state.h"

#include <assert.h>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string>

long long get_system_time_ms();
std::string json_number_or_null(bool available, double value);
std::string json_escape_string(const std::string& s);
std::string json_string_or_null(const std::string& s);
std::string json_bool(bool v);

// 实验 / 阶段采集控制
bool start_experiment_capture(const std::string& experiment_name, std::string& message);
bool stop_experiment_capture(std::string& message);
bool start_stage_capture(const std::string& stage_name, std::string& message);
bool stop_stage_capture(std::string& message);

// 各流 callback
void gaze_point_callback(const tobii_gaze_point_t* gaze_point, void* user_data);
void gaze_origin_callback(const tobii_gaze_origin_t* gaze_origin, void* user_data);
void eye_position_normalized_callback(const tobii_eye_position_normalized_t* eye_position, void* user_data);
void user_presence_callback(tobii_user_presence_status_t status, int64_t timestamp_us, void* user_data);
void head_pose_callback(const tobii_head_pose_t* head_pose, void* user_data);
void notifications_callback(const tobii_notification_t* notification, void* user_data);
void user_position_guide_callback(const tobii_user_position_guide_t* guide, void* user_data);

// 订阅 / 退订
void start_listen(tobii_device_t* device);
void stop_listen(tobii_device_t* device);

// 保留旧函数声明
void get_save_data(tobii_device_t* device, int timeLength);

#endif