#include "src/global.h"
#include "src/shared_state.h"
#include "src/save_data.h"
#include "src/connect.h"
#include "src/udp_server.h"
#include "src/web_server.h"

#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

void start_listen(tobii_device_t* device);
void stop_listen(tobii_device_t* device);

int main()
{
    std::cout << "========== MAIN START ==========" << std::endl;
    std::cout << "Hello, tobii" << std::endl;

    // 1. Create Tobii API
    std::cout << "[main] creating Tobii API..." << std::endl;
    api = api_create();
    std::cout << "[main] api_create done, api="
        << (api ? "non-null" : "nullptr") << std::endl;

    // 2. Enumerate devices (do not exit if no devices)
    std::cout << "[main] enumerating Tobii device..." << std::endl;
    char* device_url = get_device(api);
    std::cout << "[main] get_device done, url="
        << (device_url ? device_url : "nullptr") << std::endl;

    // 3. Create devices
    std::cout << "[main] creating Tobii device..." << std::endl;
    device = device_create(api, device_url);
    std::cout << "[main] device_create done, device="
        << (device ? "non-null" : "nullptr") << std::endl;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);

        g_app_state.device_connected = (device != nullptr);
        g_app_state.running = false;
        g_app_state.recording = false;
        g_app_state.stop_requested = false;

        g_app_state.streams.gaze_point = false;
        g_app_state.streams.gaze_origin = false;
        g_app_state.streams.eye_position_normalized = false;
        g_app_state.streams.user_presence = false;
        g_app_state.streams.head_pose = false;
        g_app_state.streams.notifications = false;
        g_app_state.streams.user_position_guide = false;

        g_app_state.runtime_stats.gaze_point_count = 0;
        g_app_state.runtime_stats.gaze_origin_count = 0;
        g_app_state.runtime_stats.eye_position_normalized_count = 0;
        g_app_state.runtime_stats.user_presence_count = 0;
        g_app_state.runtime_stats.head_pose_count = 0;
        g_app_state.runtime_stats.notifications_count = 0;
        g_app_state.runtime_stats.user_position_guide_count = 0;

        g_app_state.runtime_stats.last_gaze_point_time_ms = 0;
        g_app_state.runtime_stats.last_gaze_origin_time_ms = 0;
        g_app_state.runtime_stats.last_eye_position_normalized_time_ms = 0;
        g_app_state.runtime_stats.last_user_presence_time_ms = 0;
        g_app_state.runtime_stats.last_head_pose_time_ms = 0;
        g_app_state.runtime_stats.last_notifications_time_ms = 0;
        g_app_state.runtime_stats.last_user_position_guide_time_ms = 0;

        g_app_state.capture.experiment_recording = false;
        g_app_state.capture.stage_recording = false;
        g_app_state.capture.experiment_id.clear();
        g_app_state.capture.experiment_name.clear();
        g_app_state.capture.experiment_file_path.clear();
        g_app_state.capture.experiment_start_time_ms = 0;
        g_app_state.capture.experiment_stop_time_ms = 0;
        g_app_state.capture.experiment_event_count = 0;

        g_app_state.capture.stage_index = 0;
        g_app_state.capture.stage_id.clear();
        g_app_state.capture.stage_name.clear();
        g_app_state.capture.stage_file_path.clear();
        g_app_state.capture.stage_start_time_ms = 0;
        g_app_state.capture.stage_stop_time_ms = 0;
        g_app_state.capture.stage_event_count = 0;
    }

    std::cout << "[main] shared state initialized" << std::endl;

    // 4. Subscribe to all streams if the device exists
    if (device != nullptr)
    {
        std::cout << "[main] subscribing all available Tobii streams..." << std::endl;
        start_listen(device);
        std::cout << "[main] Tobii device ready." << std::endl;
    }
    else
    {
        std::cout << "[main] Running without Tobii device. Web service is still available." << std::endl;
    }

    // 5. Start UDP service
    std::cout << "[main] starting UDP server..." << std::endl;
    init_udp_server();
    std::cout << "[main] UDP server started" << std::endl;

    // 6. Start the web service thread
    std::cout << "[main] starting web server thread..." << std::endl;
    std::thread web_thread([]()
        {
            std::cout << "[web-thread] entering start_web_server()" << std::endl;
            start_web_server();
            std::cout << "[web-thread] start_web_server() returned" << std::endl;
        });

    web_thread.detach();
    std::cout << "[main] web server thread detached" << std::endl;

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.running = true;
        g_app_state.recording = false;
        g_app_state.stop_requested = false;
    }

    std::cout << "========== MAIN LOOP START ==========" << std::endl;

    long long last_stats_print_ms = get_system_time_ms();
    long long prev_gaze_point_count = 0;
    long long prev_gaze_origin_count = 0;
    long long prev_eye_position_normalized_count = 0;
    long long prev_user_presence_count = 0;
    long long prev_head_pose_count = 0;
    long long prev_notifications_count = 0;
    long long prev_user_position_guide_count = 0;

    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(g_app_state.mtx);
            if (g_app_state.stop_requested)
            {
                std::cout << "[main] stop requested, break" << std::endl;
                break;
            }
        }

        // Keep UDP reception
        int strLen = recvfrom(sock, buffer, BUF_SIZE - 1, 0, &clientAddr, &nSize);

        if (strLen == SOCKET_ERROR)
        {
            int err = WSAGetLastError();
            if (err != WSAETIMEDOUT)
            {
                std::cerr << "[main] recvfrom failed: " << err << std::endl;
            }
        }
        else if (strLen > 0)
        {
            buffer[strLen] = '\0';
            std::cout << "[main] UDP recv: " << buffer << std::endl;
        }

        // Continuous processing Tobii callbacks
        if (device != nullptr)
        {
            result = tobii_wait_for_callbacks(1, &device);
            if (result != TOBII_ERROR_NO_ERROR && result != TOBII_ERROR_TIMED_OUT)
            {
                std::cerr << "[main] tobii_wait_for_callbacks error: " << result << std::endl;
                continue;
            }

            result = tobii_device_process_callbacks(device);
            if (result != TOBII_ERROR_NO_ERROR)
            {
                std::cerr << "[main] tobii_device_process_callbacks error: " << result << std::endl;
                continue;
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        long long now_ms = get_system_time_ms();
        if (now_ms - last_stats_print_ms >= 1000)
        {
            long long gp = 0, go = 0, ep = 0, up = 0, hp = 0, nt = 0, ug = 0;
            bool gp_on = false, go_on = false, ep_on = false, up_on = false, hp_on = false, nt_on = false, ug_on = false;

            {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);

                gp = g_app_state.runtime_stats.gaze_point_count;
                go = g_app_state.runtime_stats.gaze_origin_count;
                ep = g_app_state.runtime_stats.eye_position_normalized_count;
                up = g_app_state.runtime_stats.user_presence_count;
                hp = g_app_state.runtime_stats.head_pose_count;
                nt = g_app_state.runtime_stats.notifications_count;
                ug = g_app_state.runtime_stats.user_position_guide_count;

                gp_on = g_app_state.streams.gaze_point;
                go_on = g_app_state.streams.gaze_origin;
                ep_on = g_app_state.streams.eye_position_normalized;
                up_on = g_app_state.streams.user_presence;
                hp_on = g_app_state.streams.head_pose;
                nt_on = g_app_state.streams.notifications;
                ug_on = g_app_state.streams.user_position_guide;
            }

            std::cout
                << "[Tobii] live stats | "
                << "gaze_point=" << (gp - prev_gaze_point_count) << (gp_on ? "" : "(off)") << " | "
                << "gaze_origin=" << (go - prev_gaze_origin_count) << (go_on ? "" : "(off)") << " | "
                << "eye_position_normalized=" << (ep - prev_eye_position_normalized_count) << (ep_on ? "" : "(off)") << " | "
                << "user_presence=" << (up - prev_user_presence_count) << (up_on ? "" : "(off)") << " | "
                << "head_pose=" << (hp - prev_head_pose_count) << (hp_on ? "" : "(off)") << " | "
                << "notifications=" << (nt - prev_notifications_count) << (nt_on ? "" : "(off)") << " | "
                << "user_position_guide=" << (ug - prev_user_position_guide_count) << (ug_on ? "" : "(off)")
                << std::endl;

            prev_gaze_point_count = gp;
            prev_gaze_origin_count = go;
            prev_eye_position_normalized_count = ep;
            prev_user_presence_count = up;
            prev_head_pose_count = hp;
            prev_notifications_count = nt;
            prev_user_position_guide_count = ug;

            last_stats_print_ms = now_ms;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);

        g_app_state.device_connected = false;
        g_app_state.running = false;
        g_app_state.recording = false;
        g_app_state.stop_requested = false;

        g_app_state.streams.gaze_point = false;
        g_app_state.streams.gaze_origin = false;
        g_app_state.streams.eye_position_normalized = false;
        g_app_state.streams.user_presence = false;
        g_app_state.streams.head_pose = false;
        g_app_state.streams.notifications = false;
        g_app_state.streams.user_position_guide = false;

        g_app_state.capture.experiment_recording = false;
        g_app_state.capture.stage_recording = false;
        g_app_state.capture.experiment_id.clear();
        g_app_state.capture.experiment_name.clear();
        g_app_state.capture.experiment_file_path.clear();
        g_app_state.capture.experiment_start_time_ms = 0;
        g_app_state.capture.experiment_stop_time_ms = 0;
        g_app_state.capture.experiment_event_count = 0;

        g_app_state.capture.stage_index = 0;
        g_app_state.capture.stage_id.clear();
        g_app_state.capture.stage_name.clear();
        g_app_state.capture.stage_file_path.clear();
        g_app_state.capture.stage_start_time_ms = 0;
        g_app_state.capture.stage_stop_time_ms = 0;
        g_app_state.capture.stage_event_count = 0;
    }

    std::cout << "[main] closing Tobii/UDP..." << std::endl;
    close_connect(device, api);
    close_udp_server();

    std::cout << "========== MAIN EXIT ==========" << std::endl;
    return 0;
}
