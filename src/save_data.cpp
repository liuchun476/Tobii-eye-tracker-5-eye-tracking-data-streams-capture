#include "save_data.h"
#include "shared_state.h"
#include "web_server.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

// 固定实验屏幕尺寸
static const int SCREEN_WIDTH = 2560;
static const int SCREEN_HEIGHT = 1440;

// 数据目录
static const char* DATA_ROOT = "D:\\eyetracking\\tobiieyetrack\\data";
static const char* INDEX_FILE_PATH = "D:\\eyetracking\\tobiieyetrack\\data\\index.txt";

static std::mutex g_file_mtx;

static const char* tobii_error_to_string_local(tobii_error_t err)
{
    switch (err)
    {
    case TOBII_ERROR_NO_ERROR: return "TOBII_ERROR_NO_ERROR";
    case TOBII_ERROR_INTERNAL: return "TOBII_ERROR_INTERNAL";
    case TOBII_ERROR_INSUFFICIENT_LICENSE: return "TOBII_ERROR_INSUFFICIENT_LICENSE";
    case TOBII_ERROR_NOT_SUPPORTED: return "TOBII_ERROR_NOT_SUPPORTED";
    case TOBII_ERROR_NOT_AVAILABLE: return "TOBII_ERROR_NOT_AVAILABLE";
    case TOBII_ERROR_CONNECTION_FAILED: return "TOBII_ERROR_CONNECTION_FAILED";
    case TOBII_ERROR_TIMED_OUT: return "TOBII_ERROR_TIMED_OUT";
    case TOBII_ERROR_ALLOCATION_FAILED: return "TOBII_ERROR_ALLOCATION_FAILED";
    case TOBII_ERROR_INVALID_PARAMETER: return "TOBII_ERROR_INVALID_PARAMETER";
    case TOBII_ERROR_CALIBRATION_ALREADY_STARTED: return "TOBII_ERROR_CALIBRATION_ALREADY_STARTED";
    case TOBII_ERROR_CALIBRATION_NOT_STARTED: return "TOBII_ERROR_CALIBRATION_NOT_STARTED";
    case TOBII_ERROR_ALREADY_SUBSCRIBED: return "TOBII_ERROR_ALREADY_SUBSCRIBED";
    case TOBII_ERROR_NOT_SUBSCRIBED: return "TOBII_ERROR_NOT_SUBSCRIBED";
    case TOBII_ERROR_OPERATION_FAILED: return "TOBII_ERROR_OPERATION_FAILED";
    case TOBII_ERROR_CONFLICTING_API_INSTANCES: return "TOBII_ERROR_CONFLICTING_API_INSTANCES";
    case TOBII_ERROR_CALLBACK_IN_PROGRESS: return "TOBII_ERROR_CALLBACK_IN_PROGRESS";
    case TOBII_ERROR_TOO_MANY_SUBSCRIBERS: return "TOBII_ERROR_TOO_MANY_SUBSCRIBERS";
    default: return "TOBII_ERROR_UNKNOWN";
    }
}

static void log_subscribe_result(const char* stream_name, tobii_error_t err)
{
    if (err == TOBII_ERROR_NO_ERROR)
    {
        std::cout << "[Tobii] " << stream_name << "_subscribe success" << std::endl;
    }
    else
    {
        std::cerr << "[Tobii] " << stream_name << "_subscribe failed: "
            << err << " (" << tobii_error_to_string_local(err) << ")"
            << " -- continue with remaining streams"
            << std::endl;
    }
}

static double clamp_double(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

long long get_system_time_ms()
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return (long long)ms;
}

static std::string make_time_tag()
{
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);

    std::tm tmv{};
    localtime_s(&tmv, &tt);

    std::ostringstream oss;
    oss << std::put_time(&tmv, "%Y%m%d_%H%M%S");
    return oss.str();
}

static std::string sanitize_filename(const std::string& input)
{
    if (input.empty()) return "unnamed";

    std::string s;
    for (char c : input)
    {
        bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.';

        s.push_back(ok ? c : '_');
    }

    while (!s.empty() && (s.back() == '.' || s.back() == ' '))
        s.pop_back();

    if (s.empty()) s = "unnamed";
    return s;
}

static void ensure_dir(const fs::path& p)
{
    std::error_code ec;
    fs::create_directories(p, ec);
}

std::string json_number_or_null(bool available, double value)
{
    if (!available)
        return "null";

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}

std::string json_escape_string(const std::string& s)
{
    std::ostringstream oss;
    for (char c : s)
    {
        switch (c)
        {
        case '\"': oss << "\\\""; break;
        case '\\': oss << "\\\\"; break;
        case '\b': oss << "\\b"; break;
        case '\f': oss << "\\f"; break;
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        default:
            if ((unsigned char)c < 0x20)
            {
                oss << "\\u"
                    << std::hex << std::setw(4) << std::setfill('0')
                    << (int)(unsigned char)c
                    << std::dec << std::setfill(' ');
            }
            else
            {
                oss << c;
            }
            break;
        }
    }
    return oss.str();
}

std::string json_string_or_null(const std::string& s)
{
    if (s.empty()) return "null";
    return "\"" + json_escape_string(s) + "\"";
}

std::string json_bool(bool v)
{
    return v ? "true" : "false";
}

// 修复：把 {"a":1,"b":2} 变成 "a":1,"b":2，供顶层对象拼接
static std::string unwrap_json_object(const std::string& obj_json)
{
    if (obj_json.size() >= 2 && obj_json.front() == '{' && obj_json.back() == '}')
    {
        return obj_json.substr(1, obj_json.size() - 2);
    }
    return obj_json;
}

static bool append_jsonl_record(const std::string& file_path, const std::string& message)
{
    std::lock_guard<std::mutex> lock(g_file_mtx);

    std::ofstream saveFile(file_path, std::ios::app);
    if (!saveFile.is_open())
    {
        std::cerr << "[save_data] Failed to open file: " << file_path << std::endl;
        return false;
    }

    saveFile << message << "\n";
    saveFile.close();
    return true;
}

static std::string make_capture_json_snapshot()
{
    std::lock_guard<std::mutex> lock(g_app_state.mtx);

    std::ostringstream oss;
    oss << "{"
        << "\"experiment_recording\":" << json_bool(g_app_state.capture.experiment_recording) << ","
        << "\"stage_recording\":" << json_bool(g_app_state.capture.stage_recording) << ","
        << "\"experiment_id\":" << json_string_or_null(g_app_state.capture.experiment_id) << ","
        << "\"experiment_name\":" << json_string_or_null(g_app_state.capture.experiment_name) << ","
        << "\"stage_id\":" << json_string_or_null(g_app_state.capture.stage_id) << ","
        << "\"stage_name\":" << json_string_or_null(g_app_state.capture.stage_name) << ","
        << "\"stage_index\":" << g_app_state.capture.stage_index
        << "}";
    return oss.str();
}

static std::string make_event_json_with_flat_fields(
    const std::string& event_type,
    long long system_time_ms,
    long long device_time_us,
    const std::string& flat_fields_json,
    const std::string& data_json)
{
    std::ostringstream oss;
    oss << "{"
        << "\"event_type\":\"" << event_type << "\","
        << "\"system_time_ms\":" << system_time_ms << ","
        << "\"device_time_us\":" << device_time_us << ","
        << "\"source\":\"tobii_eye_tracker_5\"";

    std::string flat = unwrap_json_object(flat_fields_json);
    if (!flat.empty())
    {
        oss << "," << flat;
    }

    oss << ",\"capture\":" << make_capture_json_snapshot()
        << ",\"data\":" << data_json
        << "}";

    return oss.str();
}

static void dispatch_stream_json(const std::string& message)
{
    bool write_experiment = false;
    bool write_stage = false;
    std::string experiment_file_path;
    std::string stage_file_path;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);

        write_experiment = g_app_state.capture.experiment_recording;
        write_stage = g_app_state.capture.stage_recording;
        experiment_file_path = g_app_state.capture.experiment_file_path;
        stage_file_path = g_app_state.capture.stage_file_path;

        if (write_experiment)
            g_app_state.capture.experiment_event_count++;

        if (write_stage)
            g_app_state.capture.stage_event_count++;

        g_app_state.recording = g_app_state.capture.experiment_recording || g_app_state.capture.stage_recording;
    }

    if (write_experiment && !experiment_file_path.empty())
    {
        append_jsonl_record(experiment_file_path, message);
    }

    if (write_stage && !stage_file_path.empty())
    {
        append_jsonl_record(stage_file_path, message);
    }

    broadcast_stream_ws(message);
}

static void emit_control_event(const std::string& event_type, const std::string& data_json)
{
    long long system_time_ms = get_system_time_ms();
    std::string message = make_event_json_with_flat_fields(
        event_type,
        system_time_ms,
        0,
        data_json,
        data_json
    );
    dispatch_stream_json(message);
}

bool start_experiment_capture(const std::string& experiment_name, std::string& message)
{
    std::string safe_name = sanitize_filename(experiment_name.empty() ? "experiment" : experiment_name);
    std::string time_tag = make_time_tag();
    std::string experiment_id = safe_name + "_" + time_tag;

    fs::path root(DATA_ROOT);
    fs::path experiment_dir = root / "experiments" / experiment_id;
    ensure_dir(experiment_dir);

    fs::path experiment_file = experiment_dir / "full_stream.jsonl";

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);

        if (g_app_state.capture.experiment_recording)
        {
            std::cout << "[capture] old experiment still recording, force stop first" << std::endl;

            g_app_state.capture.experiment_recording = false;
            g_app_state.capture.stage_recording = false;
            g_app_state.capture.experiment_id.clear();
            g_app_state.capture.experiment_name.clear();
            g_app_state.capture.experiment_file_path.clear();
            g_app_state.capture.stage_id.clear();
            g_app_state.capture.stage_name.clear();
            g_app_state.capture.stage_file_path.clear();
            g_app_state.recording = false;
        }

        g_app_state.capture.experiment_recording = true;
        g_app_state.capture.experiment_id = experiment_id;
        g_app_state.capture.experiment_name = experiment_name.empty() ? "experiment" : experiment_name;
        g_app_state.capture.experiment_file_path = experiment_file.string();
        g_app_state.capture.experiment_start_time_ms = get_system_time_ms();
        g_app_state.capture.experiment_stop_time_ms = 0;
        g_app_state.capture.experiment_event_count = 0;

        g_app_state.capture.stage_recording = false;
        g_app_state.capture.stage_index = 0;
        g_app_state.capture.stage_id.clear();
        g_app_state.capture.stage_name.clear();
        g_app_state.capture.stage_file_path.clear();
        g_app_state.capture.stage_start_time_ms = 0;
        g_app_state.capture.stage_stop_time_ms = 0;
        g_app_state.capture.stage_event_count = 0;

        g_app_state.recording = true;
    }

    {
        std::ofstream meta((experiment_dir / "experiment_meta.json").string(), std::ios::out);
        if (meta.is_open())
        {
            meta << "{\n"
                << "  \"experiment_id\": \"" << json_escape_string(experiment_id) << "\",\n"
                << "  \"experiment_name\": \"" << json_escape_string(experiment_name.empty() ? "experiment" : experiment_name) << "\",\n"
                << "  \"full_stream_file\": \"" << json_escape_string(experiment_file.string()) << "\"\n"
                << "}\n";
            meta.close();
        }
    }

    emit_control_event(
        "experiment_started",
        std::string("{")
        + "\"experiment_id\":" + json_string_or_null(experiment_id) + ","
        + "\"experiment_name\":" + json_string_or_null(experiment_name.empty() ? "experiment" : experiment_name) + ","
        + "\"experiment_file_path\":" + json_string_or_null(experiment_file.string())
        + "}"
    );

    message = experiment_file.string();
    return true;
}

bool stop_stage_capture(std::string& message)
{
    std::string stage_id;
    std::string stage_name;
    std::string stage_file_path;
    int stage_index = 0;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        if (!g_app_state.capture.stage_recording)
        {
            if (!g_app_state.capture.stage_recording)
            {
                message = "stage not recording, ignored";
                return true;
            }
        }

        stage_id = g_app_state.capture.stage_id;
        stage_name = g_app_state.capture.stage_name;
        stage_file_path = g_app_state.capture.stage_file_path;
        stage_index = g_app_state.capture.stage_index;
    }

    emit_control_event(
        "stage_stopped",
        std::string("{")
        + "\"stage_id\":" + json_string_or_null(stage_id) + ","
        + "\"stage_name\":" + json_string_or_null(stage_name) + ","
        + "\"stage_index\":" + std::to_string(stage_index) + ","
        + "\"stage_file_path\":" + json_string_or_null(stage_file_path)
        + "}"
    );

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.capture.stage_recording = false;
        g_app_state.capture.stage_stop_time_ms = get_system_time_ms();
        g_app_state.capture.stage_id.clear();
        g_app_state.capture.stage_name.clear();
        g_app_state.capture.stage_file_path.clear();
        g_app_state.capture.stage_start_time_ms = 0;
        g_app_state.capture.stage_stop_time_ms = 0;
        g_app_state.capture.stage_event_count = 0;

        g_app_state.recording = g_app_state.capture.experiment_recording;
    }

    message = stage_file_path;
    return true;
}

bool start_stage_capture(const std::string& stage_name, std::string& message)
{
    std::string experiment_id;
    int stage_index = 0;
    std::string safe_stage_name = sanitize_filename(stage_name.empty() ? "stage" : stage_name);
    std::string stage_id;
    std::string stage_file_path;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);

        if (!g_app_state.capture.experiment_recording)
        {
            message = "experiment not recording, ignored";
            return true;
        }

        if (g_app_state.capture.stage_recording)
        {
            std::cout << "[capture] old stage still recording, force close it first" << std::endl;

            g_app_state.capture.stage_recording = false;
            g_app_state.capture.stage_id.clear();
            g_app_state.capture.stage_name.clear();
            g_app_state.capture.stage_file_path.clear();
            g_app_state.capture.stage_start_time_ms = 0;
            g_app_state.capture.stage_stop_time_ms = 0;
            g_app_state.capture.stage_event_count = 0;
        }

        experiment_id = g_app_state.capture.experiment_id;
        stage_index = g_app_state.capture.stage_index + 1;
    }

    fs::path root(DATA_ROOT);
    fs::path stage_dir = root / "experiments" / experiment_id / "stages";
    ensure_dir(stage_dir);

    std::ostringstream filename;
    filename << std::setw(2) << std::setfill('0') << stage_index << "_" << safe_stage_name << ".jsonl";

    stage_id = "stage_" + std::to_string(stage_index) + "_" + make_time_tag();
    stage_file_path = (stage_dir / filename.str()).string();

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);

        g_app_state.capture.stage_recording = true;
        g_app_state.capture.stage_index = stage_index;
        g_app_state.capture.stage_id = stage_id;
        g_app_state.capture.stage_name = stage_name.empty() ? "stage" : stage_name;
        g_app_state.capture.stage_file_path = stage_file_path;
        g_app_state.capture.stage_start_time_ms = get_system_time_ms();
        g_app_state.capture.stage_stop_time_ms = 0;
        g_app_state.capture.stage_event_count = 0;

        g_app_state.recording = true;
    }

    emit_control_event(
        "stage_started",
        std::string("{")
        + "\"stage_id\":" + json_string_or_null(stage_id) + ","
        + "\"stage_name\":" + json_string_or_null(stage_name.empty() ? "stage" : stage_name) + ","
        + "\"stage_index\":" + std::to_string(stage_index) + ","
        + "\"stage_file_path\":" + json_string_or_null(stage_file_path)
        + "}"
    );

    message = stage_file_path;
    return true;
}

bool stop_experiment_capture(std::string& message)
{
    std::string tmp;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        if (!g_app_state.capture.experiment_recording)
        {
            message = "experiment not recording";
            return false;
        }
    }

    {
        bool need_stop_stage = false;
        {
            std::lock_guard<std::mutex> lock(g_app_state.mtx);
            need_stop_stage = g_app_state.capture.stage_recording;
        }
        if (need_stop_stage)
        {
            stop_stage_capture(tmp);
        }
    }

    std::string experiment_id;
    std::string experiment_name;
    std::string experiment_file_path;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        experiment_id = g_app_state.capture.experiment_id;
        experiment_name = g_app_state.capture.experiment_name;
        experiment_file_path = g_app_state.capture.experiment_file_path;
    }

    emit_control_event(
        "experiment_stopped",
        std::string("{")
        + "\"experiment_id\":" + json_string_or_null(experiment_id) + ","
        + "\"experiment_name\":" + json_string_or_null(experiment_name) + ","
        + "\"experiment_file_path\":" + json_string_or_null(experiment_file_path)
        + "}"
    );

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);

        g_app_state.capture.experiment_recording = false;
        g_app_state.capture.experiment_stop_time_ms = get_system_time_ms();

        g_app_state.capture.experiment_id.clear();
        g_app_state.capture.experiment_name.clear();
        g_app_state.capture.experiment_file_path.clear();
        g_app_state.capture.experiment_start_time_ms = 0;
        g_app_state.capture.experiment_stop_time_ms = 0;
        g_app_state.capture.experiment_event_count = 0;

        g_app_state.capture.stage_recording = false;
        g_app_state.capture.stage_index = 0;
        g_app_state.capture.stage_id.clear();
        g_app_state.capture.stage_name.clear();
        g_app_state.capture.stage_file_path.clear();
        g_app_state.capture.stage_start_time_ms = 0;
        g_app_state.capture.stage_stop_time_ms = 0;
        g_app_state.capture.stage_event_count = 0;

        g_app_state.recording = false;
    }

    message = experiment_file_path;
    return true;
}

void gaze_point_callback(const tobii_gaze_point_t* gaze_point, void* /* user_data */)
{
    long long system_time_ms = get_system_time_ms();
    long long device_time_us = (long long)gaze_point->timestamp_us;

    bool valid = (gaze_point->validity == TOBII_VALIDITY_VALID);

    double norm_x = 0.0;
    double norm_y = 0.0;
    double pixel_x = 0.0;
    double pixel_y = 0.0;
    int trigger_index = 0;
    int success_index = 0;

    if (valid)
    {
        norm_x = clamp_double((double)gaze_point->position_xy[0], 0.0, 1.0);
        norm_y = clamp_double((double)gaze_point->position_xy[1], 0.0, 1.0);
        pixel_x = norm_x * SCREEN_WIDTH;
        pixel_y = norm_y * SCREEN_HEIGHT;
    }

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);

        g_app_state.gaze_point.system_time_ms = system_time_ms;
        g_app_state.gaze_point.device_time_us = device_time_us;
        g_app_state.gaze_point.validity = (int)gaze_point->validity;
        g_app_state.gaze_point.position_xy[0] = (float)norm_x;
        g_app_state.gaze_point.position_xy[1] = (float)norm_y;
        g_app_state.gaze_point.pixel_x = pixel_x;
        g_app_state.gaze_point.pixel_y = pixel_y;
        g_app_state.gaze_point.screen_w = SCREEN_WIDTH;
        g_app_state.gaze_point.screen_h = SCREEN_HEIGHT;

        trigger_index = g_app_state.stats.trigger_index;
        success_index = g_app_state.stats.success_index;

        g_app_state.gaze_point.trigger_index = trigger_index;
        g_app_state.gaze_point.success_index = success_index;

        if (valid)
        {
            g_app_state.stats.last_trigger_success = true;
        }

        g_app_state.runtime_stats.gaze_point_count++;
        g_app_state.runtime_stats.last_gaze_point_time_ms = system_time_ms;
    }

    std::ostringstream data;
    data << "{"
        << "\"validity\":" << (int)gaze_point->validity << ","
        << "\"position_xy\":["
        << json_number_or_null(valid, norm_x) << ","
        << json_number_or_null(valid, norm_y) << "],"
        << "\"pixel_x\":" << json_number_or_null(valid, pixel_x) << ","
        << "\"pixel_y\":" << json_number_or_null(valid, pixel_y) << ","
        << "\"screen_w\":" << SCREEN_WIDTH << ","
        << "\"screen_h\":" << SCREEN_HEIGHT << ","
        << "\"trigger_index\":" << trigger_index << ","
        << "\"success_index\":" << success_index
        << "}";

    dispatch_stream_json(make_event_json_with_flat_fields(
        "gaze_point",
        system_time_ms,
        device_time_us,
        data.str(),
        data.str()
    ));
}

void gaze_origin_callback(const tobii_gaze_origin_t* gaze_origin, void* /* user_data */)
{
    long long system_time_ms = get_system_time_ms();
    long long device_time_us = (long long)gaze_origin->timestamp_us;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.gaze_origin.system_time_ms = system_time_ms;
        g_app_state.gaze_origin.device_time_us = device_time_us;
        g_app_state.gaze_origin.left_validity = (int)gaze_origin->left_validity;
        g_app_state.gaze_origin.left_xyz[0] = gaze_origin->left_xyz[0];
        g_app_state.gaze_origin.left_xyz[1] = gaze_origin->left_xyz[1];
        g_app_state.gaze_origin.left_xyz[2] = gaze_origin->left_xyz[2];
        g_app_state.gaze_origin.right_validity = (int)gaze_origin->right_validity;
        g_app_state.gaze_origin.right_xyz[0] = gaze_origin->right_xyz[0];
        g_app_state.gaze_origin.right_xyz[1] = gaze_origin->right_xyz[1];
        g_app_state.gaze_origin.right_xyz[2] = gaze_origin->right_xyz[2];

        g_app_state.runtime_stats.gaze_origin_count++;
        g_app_state.runtime_stats.last_gaze_origin_time_ms = system_time_ms;
    }

    std::ostringstream data;
    data << "{"
        << "\"left_validity\":" << (int)gaze_origin->left_validity << ","
        << "\"left_xyz\":["
        << gaze_origin->left_xyz[0] << ","
        << gaze_origin->left_xyz[1] << ","
        << gaze_origin->left_xyz[2] << "],"
        << "\"right_validity\":" << (int)gaze_origin->right_validity << ","
        << "\"right_xyz\":["
        << gaze_origin->right_xyz[0] << ","
        << gaze_origin->right_xyz[1] << ","
        << gaze_origin->right_xyz[2] << "]"
        << "}";

    dispatch_stream_json(make_event_json_with_flat_fields(
        "gaze_origin",
        system_time_ms,
        device_time_us,
        data.str(),
        data.str()
    ));
}

void eye_position_normalized_callback(const tobii_eye_position_normalized_t* eye_position, void* /* user_data */)
{
    long long system_time_ms = get_system_time_ms();
    long long device_time_us = (long long)eye_position->timestamp_us;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.eye_position_normalized.system_time_ms = system_time_ms;
        g_app_state.eye_position_normalized.device_time_us = device_time_us;
        g_app_state.eye_position_normalized.left_validity = (int)eye_position->left_validity;
        g_app_state.eye_position_normalized.left_xyz[0] = eye_position->left_xyz[0];
        g_app_state.eye_position_normalized.left_xyz[1] = eye_position->left_xyz[1];
        g_app_state.eye_position_normalized.left_xyz[2] = eye_position->left_xyz[2];
        g_app_state.eye_position_normalized.right_validity = (int)eye_position->right_validity;
        g_app_state.eye_position_normalized.right_xyz[0] = eye_position->right_xyz[0];
        g_app_state.eye_position_normalized.right_xyz[1] = eye_position->right_xyz[1];
        g_app_state.eye_position_normalized.right_xyz[2] = eye_position->right_xyz[2];

        g_app_state.runtime_stats.eye_position_normalized_count++;
        g_app_state.runtime_stats.last_eye_position_normalized_time_ms = system_time_ms;
    }

    std::ostringstream data;
    data << "{"
        << "\"left_validity\":" << (int)eye_position->left_validity << ","
        << "\"left_xyz\":["
        << eye_position->left_xyz[0] << ","
        << eye_position->left_xyz[1] << ","
        << eye_position->left_xyz[2] << "],"
        << "\"right_validity\":" << (int)eye_position->right_validity << ","
        << "\"right_xyz\":["
        << eye_position->right_xyz[0] << ","
        << eye_position->right_xyz[1] << ","
        << eye_position->right_xyz[2] << "]"
        << "}";

    dispatch_stream_json(make_event_json_with_flat_fields(
        "eye_position_normalized",
        system_time_ms,
        device_time_us,
        data.str(),
        data.str()
    ));
}

void user_presence_callback(tobii_user_presence_status_t status, int64_t timestamp_us, void* /* user_data */)
{
    long long system_time_ms = get_system_time_ms();
    long long device_time_us = (long long)timestamp_us;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.user_presence.system_time_ms = system_time_ms;
        g_app_state.user_presence.device_time_us = device_time_us;
        g_app_state.user_presence.status = (int)status;

        g_app_state.runtime_stats.user_presence_count++;
        g_app_state.runtime_stats.last_user_presence_time_ms = system_time_ms;
    }

    std::ostringstream data;
    data << "{"
        << "\"status\":" << (int)status
        << "}";

    dispatch_stream_json(make_event_json_with_flat_fields(
        "user_presence",
        system_time_ms,
        device_time_us,
        data.str(),
        data.str()
    ));
}

void head_pose_callback(const tobii_head_pose_t* head_pose, void* /* user_data */)
{
    long long system_time_ms = get_system_time_ms();
    long long device_time_us = (long long)head_pose->timestamp_us;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.head_pose.system_time_ms = system_time_ms;
        g_app_state.head_pose.device_time_us = device_time_us;
        g_app_state.head_pose.position_validity = (int)head_pose->position_validity;
        g_app_state.head_pose.position_xyz[0] = head_pose->position_xyz[0];
        g_app_state.head_pose.position_xyz[1] = head_pose->position_xyz[1];
        g_app_state.head_pose.position_xyz[2] = head_pose->position_xyz[2];
        g_app_state.head_pose.rotation_validity_xyz[0] = (int)head_pose->rotation_validity_xyz[0];
        g_app_state.head_pose.rotation_validity_xyz[1] = (int)head_pose->rotation_validity_xyz[1];
        g_app_state.head_pose.rotation_validity_xyz[2] = (int)head_pose->rotation_validity_xyz[2];
        g_app_state.head_pose.rotation_xyz[0] = head_pose->rotation_xyz[0];
        g_app_state.head_pose.rotation_xyz[1] = head_pose->rotation_xyz[1];
        g_app_state.head_pose.rotation_xyz[2] = head_pose->rotation_xyz[2];

        g_app_state.runtime_stats.head_pose_count++;
        g_app_state.runtime_stats.last_head_pose_time_ms = system_time_ms;
    }

    std::ostringstream data;
    data << "{"
        << "\"position_validity\":" << (int)head_pose->position_validity << ","
        << "\"position_xyz\":["
        << head_pose->position_xyz[0] << ","
        << head_pose->position_xyz[1] << ","
        << head_pose->position_xyz[2] << "],"
        << "\"rotation_validity_xyz\":["
        << (int)head_pose->rotation_validity_xyz[0] << ","
        << (int)head_pose->rotation_validity_xyz[1] << ","
        << (int)head_pose->rotation_validity_xyz[2] << "],"
        << "\"rotation_xyz\":["
        << head_pose->rotation_xyz[0] << ","
        << head_pose->rotation_xyz[1] << ","
        << head_pose->rotation_xyz[2] << "]"
        << "}";

    dispatch_stream_json(make_event_json_with_flat_fields(
        "head_pose",
        system_time_ms,
        device_time_us,
        data.str(),
        data.str()
    ));
}

void notifications_callback(const tobii_notification_t* notification, void* /* user_data */)
{
    long long system_time_ms = get_system_time_ms();

    float value_float = 0.0f;
    unsigned int value_uint = 0;
    int value_state = 0;
    int value_enabled_eye = 0;
    std::string value_string;

    switch (notification->value_type)
    {
    case TOBII_NOTIFICATION_VALUE_TYPE_FLOAT:
        value_float = notification->value.float_;
        break;
    case TOBII_NOTIFICATION_VALUE_TYPE_UINT:
        value_uint = notification->value.uint_;
        break;
    case TOBII_NOTIFICATION_VALUE_TYPE_STATE:
        value_state = (int)notification->value.state;
        break;
    case TOBII_NOTIFICATION_VALUE_TYPE_ENABLED_EYE:
        value_enabled_eye = (int)notification->value.enabled_eye;
        break;
    case TOBII_NOTIFICATION_VALUE_TYPE_STRING:
        value_string = notification->value.string_;
        break;
    default:
        break;
    }

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.notification.system_time_ms = system_time_ms;
        g_app_state.notification.type = (int)notification->type;
        g_app_state.notification.value_type = (int)notification->value_type;
        g_app_state.notification.value_float = value_float;
        g_app_state.notification.value_uint = value_uint;
        g_app_state.notification.value_state = value_state;
        g_app_state.notification.value_enabled_eye = value_enabled_eye;
        g_app_state.notification.value_string = value_string;

        g_app_state.runtime_stats.notifications_count++;
        g_app_state.runtime_stats.last_notifications_time_ms = system_time_ms;
    }

    std::ostringstream data;
    data << "{"
        << "\"type\":" << (int)notification->type << ","
        << "\"value_type\":" << (int)notification->value_type << ","
        << "\"value_float\":" << value_float << ","
        << "\"value_uint\":" << value_uint << ","
        << "\"value_state\":" << value_state << ","
        << "\"value_enabled_eye\":" << value_enabled_eye << ","
        << "\"value_string\":\"" << json_escape_string(value_string) << "\""
        << "}";

    dispatch_stream_json(make_event_json_with_flat_fields(
        "notification",
        system_time_ms,
        0,
        data.str(),
        data.str()
    ));
}

void user_position_guide_callback(const tobii_user_position_guide_t* guide, void* /* user_data */)
{
    long long system_time_ms = get_system_time_ms();
    long long device_time_us = (long long)guide->timestamp_us;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.user_position_guide.system_time_ms = system_time_ms;
        g_app_state.user_position_guide.device_time_us = device_time_us;
        g_app_state.user_position_guide.left_position_validity = (int)guide->left_position_validity;
        g_app_state.user_position_guide.left_position_normalized_xyz[0] = guide->left_position_normalized_xyz[0];
        g_app_state.user_position_guide.left_position_normalized_xyz[1] = guide->left_position_normalized_xyz[1];
        g_app_state.user_position_guide.left_position_normalized_xyz[2] = guide->left_position_normalized_xyz[2];
        g_app_state.user_position_guide.right_position_validity = (int)guide->right_position_validity;
        g_app_state.user_position_guide.right_position_normalized_xyz[0] = guide->right_position_normalized_xyz[0];
        g_app_state.user_position_guide.right_position_normalized_xyz[1] = guide->right_position_normalized_xyz[1];
        g_app_state.user_position_guide.right_position_normalized_xyz[2] = guide->right_position_normalized_xyz[2];

        g_app_state.runtime_stats.user_position_guide_count++;
        g_app_state.runtime_stats.last_user_position_guide_time_ms = system_time_ms;
    }

    std::ostringstream data;
    data << "{"
        << "\"left_position_validity\":" << (int)guide->left_position_validity << ","
        << "\"left_position_normalized_xyz\":["
        << guide->left_position_normalized_xyz[0] << ","
        << guide->left_position_normalized_xyz[1] << ","
        << guide->left_position_normalized_xyz[2] << "],"
        << "\"right_position_validity\":" << (int)guide->right_position_validity << ","
        << "\"right_position_normalized_xyz\":["
        << guide->right_position_normalized_xyz[0] << ","
        << guide->right_position_normalized_xyz[1] << ","
        << guide->right_position_normalized_xyz[2] << "]"
        << "}";

    dispatch_stream_json(make_event_json_with_flat_fields(
        "user_position_guide",
        system_time_ms,
        device_time_us,
        data.str(),
        data.str()
    ));
}

void start_listen(tobii_device_t* device)
{
    if (device == NULL)
    {
        std::cerr << "[Tobii] start_listen skipped: device is null" << std::endl;
        return;
    }

    int success_count = 0;
    int fail_count = 0;

    result = tobii_gaze_point_subscribe(device, gaze_point_callback, 0);
    log_subscribe_result("gaze_point", result);
    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.streams.gaze_point = (result == TOBII_ERROR_NO_ERROR);
    }
    if (result == TOBII_ERROR_NO_ERROR) success_count++; else fail_count++;

    result = tobii_gaze_origin_subscribe(device, gaze_origin_callback, 0);
    log_subscribe_result("gaze_origin", result);
    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.streams.gaze_origin = (result == TOBII_ERROR_NO_ERROR);
    }
    if (result == TOBII_ERROR_NO_ERROR) success_count++; else fail_count++;

    result = tobii_eye_position_normalized_subscribe(device, eye_position_normalized_callback, 0);
    log_subscribe_result("eye_position_normalized", result);
    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.streams.eye_position_normalized = (result == TOBII_ERROR_NO_ERROR);
    }
    if (result == TOBII_ERROR_NO_ERROR) success_count++; else fail_count++;

    result = tobii_user_presence_subscribe(device, user_presence_callback, 0);
    log_subscribe_result("user_presence", result);
    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.streams.user_presence = (result == TOBII_ERROR_NO_ERROR);
    }
    if (result == TOBII_ERROR_NO_ERROR) success_count++; else fail_count++;

    result = tobii_head_pose_subscribe(device, head_pose_callback, 0);
    log_subscribe_result("head_pose", result);
    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.streams.head_pose = (result == TOBII_ERROR_NO_ERROR);
    }
    if (result == TOBII_ERROR_NO_ERROR) success_count++; else fail_count++;

    result = tobii_notifications_subscribe(device, notifications_callback, 0);
    log_subscribe_result("notifications", result);
    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.streams.notifications = (result == TOBII_ERROR_NO_ERROR);
    }
    if (result == TOBII_ERROR_NO_ERROR) success_count++; else fail_count++;

    result = tobii_user_position_guide_subscribe(device, user_position_guide_callback, 0);
    log_subscribe_result("user_position_guide", result);
    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.streams.user_position_guide = (result == TOBII_ERROR_NO_ERROR);
    }
    if (result == TOBII_ERROR_NO_ERROR) success_count++; else fail_count++;

    std::cout << "[Tobii] subscribe summary: success=" << success_count
        << ", failed=" << fail_count << std::endl;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        std::cout
            << "[Tobii] stream states => "
            << "gaze_point=" << (g_app_state.streams.gaze_point ? "true" : "false") << ", "
            << "gaze_origin=" << (g_app_state.streams.gaze_origin ? "true" : "false") << ", "
            << "eye_position_normalized=" << (g_app_state.streams.eye_position_normalized ? "true" : "false") << ", "
            << "user_presence=" << (g_app_state.streams.user_presence ? "true" : "false") << ", "
            << "head_pose=" << (g_app_state.streams.head_pose ? "true" : "false") << ", "
            << "notifications=" << (g_app_state.streams.notifications ? "true" : "false") << ", "
            << "user_position_guide=" << (g_app_state.streams.user_position_guide ? "true" : "false")
            << std::endl;
    }

    std::cout << "[Tobii] start_listen finished, data collection will continue on all successfully subscribed streams" << std::endl;
}

void stop_listen(tobii_device_t* device)
{
    if (device == NULL)
    {
        std::cerr << "[Tobii] stop_listen skipped: device is null" << std::endl;
        return;
    }

    StreamSubscriptionState streams_snapshot;
    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        streams_snapshot = g_app_state.streams;
    }

    if (streams_snapshot.gaze_point)
        tobii_gaze_point_unsubscribe(device);

    if (streams_snapshot.gaze_origin)
        tobii_gaze_origin_unsubscribe(device);

    if (streams_snapshot.eye_position_normalized)
        tobii_eye_position_normalized_unsubscribe(device);

    if (streams_snapshot.user_presence)
        tobii_user_presence_unsubscribe(device);

    if (streams_snapshot.head_pose)
        tobii_head_pose_unsubscribe(device);

    if (streams_snapshot.notifications)
        tobii_notifications_unsubscribe(device);

    if (streams_snapshot.user_position_guide)
        tobii_user_position_guide_unsubscribe(device);

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.streams.gaze_point = false;
        g_app_state.streams.gaze_origin = false;
        g_app_state.streams.eye_position_normalized = false;
        g_app_state.streams.user_presence = false;
        g_app_state.streams.head_pose = false;
        g_app_state.streams.notifications = false;
        g_app_state.streams.user_position_guide = false;
    }

    std::cout << "[Tobii] all successfully subscribed streams unsubscribed" << std::endl;
}

void get_save_data(tobii_device_t* device, int timeLength)
{
    if (device == NULL || timeLength <= 0)
        return;

    for (int i = 0; i < timeLength; i++)
    {
        {
            std::lock_guard<std::mutex> lock(g_app_state.mtx);
            if (g_app_state.stop_requested)
            {
                break;
            }
        }

        result = tobii_wait_for_callbacks(1, &device);
        if (result != TOBII_ERROR_NO_ERROR && result != TOBII_ERROR_TIMED_OUT)
        {
            std::cerr << "[get_save_data] tobii_wait_for_callbacks failed: " << result << std::endl;
            continue;
        }

        result = tobii_device_process_callbacks(device);
        if (result != TOBII_ERROR_NO_ERROR)
        {
            std::cerr << "[get_save_data] tobii_device_process_callbacks failed: " << result << std::endl;
            continue;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);

        if (g_app_state.stats.last_trigger_success)
        {
            if (g_app_state.stats.success_index < SUCCESS_TIME)
            {
                g_app_state.stats.success[g_app_state.stats.success_index] = g_app_state.stats.trigger_index;

                std::ofstream saveFile(INDEX_FILE_PATH, std::ios::app);
                if (saveFile.is_open())
                {
                    saveFile << g_app_state.stats.trigger_index << "\n";
                    saveFile.close();
                }

                g_app_state.stats.success_index++;
            }
        }

        g_app_state.stats.trigger_index++;
        g_app_state.stats.last_trigger_success = false;
        g_app_state.stop_requested = false;

        g_app_state.gaze_point.trigger_index = g_app_state.stats.trigger_index;
        g_app_state.gaze_point.success_index = g_app_state.stats.success_index;
    }
}