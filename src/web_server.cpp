#define CROW_MAIN
#define ASIO_STANDALONE
#include "crow.h"

#include "save_data.h"
#include "shared_state.h"
#include "web_server.h"

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>

static std::mutex g_ws_clients_mtx;
static std::unordered_set<crow::websocket::connection*> g_ws_clients;

static void add_cors_headers(crow::response& res)
{
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
}

static crow::response make_json_response(int code, const std::string& body)
{
    crow::response res;
    res.code = code;
    res.set_header("Content-Type", "application/json; charset=utf-8");
    add_cors_headers(res);
    res.write(body);
    return res;
}

static crow::response make_options_response()
{
    crow::response res;
    res.code = 200;
    add_cors_headers(res);
    return res;
}

static std::string json_ok(bool ok)
{
    return ok ? "true" : "false";
}

int get_ws_client_count()
{
    std::lock_guard<std::mutex> lock(g_ws_clients_mtx);
    return (int)g_ws_clients.size();
}

void broadcast_stream_ws(const std::string& message)
{
    std::lock_guard<std::mutex> lock(g_ws_clients_mtx);

    for (auto* conn : g_ws_clients)
    {
        if (conn != nullptr)
        {
            try
            {
                conn->send_text(message);
            }
            catch (...)
            {
                std::cout << "[WS] send_text exception ignored" << std::endl;
            }
        }
    }
}

void start_web_server()
{
    std::cout << "[web] start_web_server entered" << std::endl;

    crow::SimpleApp app;

    CROW_ROUTE(app, "/")([]()
        {
            crow::response res(200, "hello");
            add_cors_headers(res);
            return res;
        });

    CROW_ROUTE(app, "/ping").methods(crow::HTTPMethod::GET)([]()
        {
            return make_json_response(200, "{\"ok\":true}");
        });

    // -------- OPTIONS 预检：显式处理最重要的 POST 路由 --------
    CROW_ROUTE(app, "/api/experiment/start").methods(crow::HTTPMethod::OPTIONS)
        ([]() { return make_options_response(); });

    CROW_ROUTE(app, "/api/experiment/stop").methods(crow::HTTPMethod::OPTIONS)
        ([]() { return make_options_response(); });

    CROW_ROUTE(app, "/api/stage/start").methods(crow::HTTPMethod::OPTIONS)
        ([]() { return make_options_response(); });

    CROW_ROUTE(app, "/api/stage/stop").methods(crow::HTTPMethod::OPTIONS)
        ([]() { return make_options_response(); });

    CROW_ROUTE(app, "/api/start").methods(crow::HTTPMethod::OPTIONS)
        ([]() { return make_options_response(); });

    CROW_ROUTE(app, "/api/stop").methods(crow::HTTPMethod::OPTIONS)
        ([]() { return make_options_response(); });

    CROW_ROUTE(app, "/api/request_stop").methods(crow::HTTPMethod::OPTIONS)
        ([]() { return make_options_response(); });

    // -------- 通用兜底 OPTIONS --------
    CROW_ROUTE(app, "/api/<path>").methods(crow::HTTPMethod::OPTIONS)
        ([](const std::string&)
            {
                return make_options_response();
            });

    CROW_ROUTE(app, "/api/status").methods(crow::HTTPMethod::GET)
        ([]()
            {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);

                std::ostringstream oss;
                oss << "{"
                    << "\"running\":" << (g_app_state.running ? "true" : "false") << ","
                    << "\"device_connected\":" << (g_app_state.device_connected ? "true" : "false") << ","
                    << "\"recording\":" << (g_app_state.recording ? "true" : "false") << ","
                    << "\"experiment_recording\":" << (g_app_state.capture.experiment_recording ? "true" : "false") << ","
                    << "\"stage_recording\":" << (g_app_state.capture.stage_recording ? "true" : "false") << ","
                    << "\"experiment_id\":" << json_string_or_null(g_app_state.capture.experiment_id) << ","
                    << "\"experiment_name\":" << json_string_or_null(g_app_state.capture.experiment_name) << ","
                    << "\"experiment_file_path\":" << json_string_or_null(g_app_state.capture.experiment_file_path) << ","
                    << "\"stage_id\":" << json_string_or_null(g_app_state.capture.stage_id) << ","
                    << "\"stage_name\":" << json_string_or_null(g_app_state.capture.stage_name) << ","
                    << "\"stage_file_path\":" << json_string_or_null(g_app_state.capture.stage_file_path) << ","
                    << "\"stage_index\":" << g_app_state.capture.stage_index << ","
                    << "\"trigger_index\":" << g_app_state.stats.trigger_index << ","
                    << "\"success_index\":" << g_app_state.stats.success_index << ","
                    << "\"ws_clients\":" << get_ws_client_count() << ","
                    << "\"streams\":{"
                    << "\"gaze_point\":" << (g_app_state.streams.gaze_point ? "true" : "false") << ","
                    << "\"gaze_origin\":" << (g_app_state.streams.gaze_origin ? "true" : "false") << ","
                    << "\"eye_position_normalized\":" << (g_app_state.streams.eye_position_normalized ? "true" : "false") << ","
                    << "\"user_presence\":" << (g_app_state.streams.user_presence ? "true" : "false") << ","
                    << "\"head_pose\":" << (g_app_state.streams.head_pose ? "true" : "false") << ","
                    << "\"notifications\":" << (g_app_state.streams.notifications ? "true" : "false") << ","
                    << "\"user_position_guide\":" << (g_app_state.streams.user_position_guide ? "true" : "false")
                    << "}"
                    << "}";

                return make_json_response(200, oss.str());
            });

    CROW_ROUTE(app, "/api/latest").methods(crow::HTTPMethod::GET)
        ([]()
            {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);

                std::ostringstream oss;
                oss << "{"
                    << "\"event_type\":\"latest_snapshot\","
                    << "\"capture\":{"
                    << "\"experiment_recording\":" << (g_app_state.capture.experiment_recording ? "true" : "false") << ","
                    << "\"stage_recording\":" << (g_app_state.capture.stage_recording ? "true" : "false") << ","
                    << "\"experiment_id\":" << json_string_or_null(g_app_state.capture.experiment_id) << ","
                    << "\"experiment_name\":" << json_string_or_null(g_app_state.capture.experiment_name) << ","
                    << "\"stage_id\":" << json_string_or_null(g_app_state.capture.stage_id) << ","
                    << "\"stage_name\":" << json_string_or_null(g_app_state.capture.stage_name) << ","
                    << "\"stage_index\":" << g_app_state.capture.stage_index
                    << "},"

                    << "\"gaze_point\":{"
                    << "\"system_time_ms\":" << g_app_state.gaze_point.system_time_ms << ","
                    << "\"device_time_us\":" << g_app_state.gaze_point.device_time_us << ","
                    << "\"validity\":" << g_app_state.gaze_point.validity << ","
                    << "\"position_xy\":["
                    << g_app_state.gaze_point.position_xy[0] << ","
                    << g_app_state.gaze_point.position_xy[1] << "],"
                    << "\"pixel_x\":" << g_app_state.gaze_point.pixel_x << ","
                    << "\"pixel_y\":" << g_app_state.gaze_point.pixel_y << ","
                    << "\"screen_w\":" << g_app_state.gaze_point.screen_w << ","
                    << "\"screen_h\":" << g_app_state.gaze_point.screen_h << ","
                    << "\"trigger_index\":" << g_app_state.gaze_point.trigger_index << ","
                    << "\"success_index\":" << g_app_state.gaze_point.success_index
                    << "},"

                    << "\"gaze_origin\":{"
                    << "\"system_time_ms\":" << g_app_state.gaze_origin.system_time_ms << ","
                    << "\"device_time_us\":" << g_app_state.gaze_origin.device_time_us << ","
                    << "\"left_validity\":" << g_app_state.gaze_origin.left_validity << ","
                    << "\"left_xyz\":["
                    << g_app_state.gaze_origin.left_xyz[0] << ","
                    << g_app_state.gaze_origin.left_xyz[1] << ","
                    << g_app_state.gaze_origin.left_xyz[2] << "],"
                    << "\"right_validity\":" << g_app_state.gaze_origin.right_validity << ","
                    << "\"right_xyz\":["
                    << g_app_state.gaze_origin.right_xyz[0] << ","
                    << g_app_state.gaze_origin.right_xyz[1] << ","
                    << g_app_state.gaze_origin.right_xyz[2] << "]"
                    << "},"

                    << "\"eye_position_normalized\":{"
                    << "\"system_time_ms\":" << g_app_state.eye_position_normalized.system_time_ms << ","
                    << "\"device_time_us\":" << g_app_state.eye_position_normalized.device_time_us << ","
                    << "\"left_validity\":" << g_app_state.eye_position_normalized.left_validity << ","
                    << "\"left_xyz\":["
                    << g_app_state.eye_position_normalized.left_xyz[0] << ","
                    << g_app_state.eye_position_normalized.left_xyz[1] << ","
                    << g_app_state.eye_position_normalized.left_xyz[2] << "],"
                    << "\"right_validity\":" << g_app_state.eye_position_normalized.right_validity << ","
                    << "\"right_xyz\":["
                    << g_app_state.eye_position_normalized.right_xyz[0] << ","
                    << g_app_state.eye_position_normalized.right_xyz[1] << ","
                    << g_app_state.eye_position_normalized.right_xyz[2] << "]"
                    << "},"

                    << "\"head_pose\":{"
                    << "\"system_time_ms\":" << g_app_state.head_pose.system_time_ms << ","
                    << "\"device_time_us\":" << g_app_state.head_pose.device_time_us << ","
                    << "\"position_validity\":" << g_app_state.head_pose.position_validity << ","
                    << "\"position_xyz\":["
                    << g_app_state.head_pose.position_xyz[0] << ","
                    << g_app_state.head_pose.position_xyz[1] << ","
                    << g_app_state.head_pose.position_xyz[2] << "],"
                    << "\"rotation_validity_xyz\":["
                    << g_app_state.head_pose.rotation_validity_xyz[0] << ","
                    << g_app_state.head_pose.rotation_validity_xyz[1] << ","
                    << g_app_state.head_pose.rotation_validity_xyz[2] << "],"
                    << "\"rotation_xyz\":["
                    << g_app_state.head_pose.rotation_xyz[0] << ","
                    << g_app_state.head_pose.rotation_xyz[1] << ","
                    << g_app_state.head_pose.rotation_xyz[2] << "]"
                    << "},"

                    << "\"user_presence\":{"
                    << "\"system_time_ms\":" << g_app_state.user_presence.system_time_ms << ","
                    << "\"device_time_us\":" << g_app_state.user_presence.device_time_us << ","
                    << "\"status\":" << g_app_state.user_presence.status
                    << "},"

                    << "\"user_position_guide\":{"
                    << "\"system_time_ms\":" << g_app_state.user_position_guide.system_time_ms << ","
                    << "\"device_time_us\":" << g_app_state.user_position_guide.device_time_us << ","
                    << "\"left_position_validity\":" << g_app_state.user_position_guide.left_position_validity << ","
                    << "\"left_position_normalized_xyz\":["
                    << g_app_state.user_position_guide.left_position_normalized_xyz[0] << ","
                    << g_app_state.user_position_guide.left_position_normalized_xyz[1] << ","
                    << g_app_state.user_position_guide.left_position_normalized_xyz[2] << "],"
                    << "\"right_position_validity\":" << g_app_state.user_position_guide.right_position_validity << ","
                    << "\"right_position_normalized_xyz\":["
                    << g_app_state.user_position_guide.right_position_normalized_xyz[0] << ","
                    << g_app_state.user_position_guide.right_position_normalized_xyz[1] << ","
                    << g_app_state.user_position_guide.right_position_normalized_xyz[2] << "]"
                    << "},"

                    << "\"notification\":{"
                    << "\"system_time_ms\":" << g_app_state.notification.system_time_ms << ","
                    << "\"type\":" << g_app_state.notification.type << ","
                    << "\"value_type\":" << g_app_state.notification.value_type << ","
                    << "\"value_float\":" << g_app_state.notification.value_float << ","
                    << "\"value_uint\":" << g_app_state.notification.value_uint << ","
                    << "\"value_state\":" << g_app_state.notification.value_state << ","
                    << "\"value_enabled_eye\":" << g_app_state.notification.value_enabled_eye << ","
                    << "\"value_string\":\"" << json_escape_string(g_app_state.notification.value_string) << "\""
                    << "}"

                    << "}";

                return make_json_response(200, oss.str());
            });

    CROW_ROUTE(app, "/api/experiment/start").methods(crow::HTTPMethod::POST)
        ([](const crow::request& req)
            {
                std::string experiment_name = "experiment";
                if (!req.body.empty())
                {
                    auto body = crow::json::load(req.body);
                    if (body && body.has("experiment_name") && body["experiment_name"].t() == crow::json::type::String)
                    {
                        experiment_name = body["experiment_name"].s();
                    }
                }

                std::string message;
                bool ok = start_experiment_capture(experiment_name, message);

                std::ostringstream oss;
                oss << "{"
                    << "\"ok\":" << json_ok(ok) << ","
                    << "\"experiment_name\":" << json_string_or_null(experiment_name) << ","
                    << "\"path\":" << json_string_or_null(message)
                    << "}";

                return make_json_response(ok ? 200 : 400, oss.str());
            });

    CROW_ROUTE(app, "/api/experiment/stop").methods(crow::HTTPMethod::POST)
        ([]()
            {
                std::string message;
                bool ok = stop_experiment_capture(message);

                std::ostringstream oss;
                oss << "{"
                    << "\"ok\":" << json_ok(ok) << ","
                    << "\"path\":" << json_string_or_null(message)
                    << "}";

                return make_json_response(ok ? 200 : 400, oss.str());
            });

    CROW_ROUTE(app, "/api/stage/start").methods(crow::HTTPMethod::POST)
        ([](const crow::request& req)
            {
                std::string stage_name = "stage";
                if (!req.body.empty())
                {
                    auto body = crow::json::load(req.body);
                    if (body && body.has("stage_name") && body["stage_name"].t() == crow::json::type::String)
                    {
                        stage_name = body["stage_name"].s();
                    }
                }

                std::string message;
                bool ok = start_stage_capture(stage_name, message);

                std::ostringstream oss;
                oss << "{"
                    << "\"ok\":" << json_ok(ok) << ","
                    << "\"stage_name\":" << json_string_or_null(stage_name) << ","
                    << "\"path\":" << json_string_or_null(message)
                    << "}";

                return make_json_response(ok ? 200 : 400, oss.str());
            });

    CROW_ROUTE(app, "/api/stage/stop").methods(crow::HTTPMethod::POST)
        ([]()
            {
                std::string message;
                bool ok = stop_stage_capture(message);

                std::ostringstream oss;
                oss << "{"
                    << "\"ok\":" << json_ok(ok) << ","
                    << "\"path\":" << json_string_or_null(message)
                    << "}";

                return make_json_response(ok ? 200 : 400, oss.str());
            });

    // 兼容旧接口
    CROW_ROUTE(app, "/api/start").methods(crow::HTTPMethod::POST)
        ([]()
            {
                std::string message;
                bool ok = start_experiment_capture("experiment", message);

                std::string body = std::string("{\"ok\":") + json_ok(ok)
                    + ",\"path\":" + json_string_or_null(message) + "}";

                return make_json_response(ok ? 200 : 400, body);
            });

    CROW_ROUTE(app, "/api/stop").methods(crow::HTTPMethod::POST)
        ([]()
            {
                std::string message;
                bool ok = stop_experiment_capture(message);

                std::string body = std::string("{\"ok\":") + json_ok(ok)
                    + ",\"path\":" + json_string_or_null(message) + "}";

                return make_json_response(ok ? 200 : 400, body);
            });

    CROW_ROUTE(app, "/api/request_stop").methods(crow::HTTPMethod::POST)
        ([]()
            {
                {
                    std::lock_guard<std::mutex> lock(g_app_state.mtx);
                    g_app_state.stop_requested = true;
                }

                return make_json_response(200, "{\"ok\":true,\"stop_requested\":true}");
            });

    CROW_WEBSOCKET_ROUTE(app, "/ws")
        .onopen([](crow::websocket::connection& conn)
            {
                std::lock_guard<std::mutex> lock(g_ws_clients_mtx);
                g_ws_clients.insert(&conn);
                std::cout << "[WS] connected. count=" << g_ws_clients.size() << std::endl;
            })
        .onclose([](crow::websocket::connection& conn, const std::string& reason, uint16_t close_code)
            {
                std::lock_guard<std::mutex> lock(g_ws_clients_mtx);
                g_ws_clients.erase(&conn);
                std::cout << "[WS] disconnected. count=" << g_ws_clients.size()
                    << ", reason=" << reason
                    << ", code=" << close_code << std::endl;
            })
        .onmessage([](crow::websocket::connection& conn, const std::string& data, bool is_binary)
            {
                std::cout << "[WS] recv: " << data
                    << ", binary=" << (is_binary ? "true" : "false") << std::endl;

                if (!is_binary && data.find("\"status\"") != std::string::npos)
                {
                    std::lock_guard<std::mutex> lock(g_app_state.mtx);

                    bool streaming =
                        g_app_state.streams.gaze_point ||
                        g_app_state.streams.gaze_origin ||
                        g_app_state.streams.eye_position_normalized ||
                        g_app_state.streams.user_presence ||
                        g_app_state.streams.head_pose ||
                        g_app_state.streams.notifications ||
                        g_app_state.streams.user_position_guide;

                    std::ostringstream oss;
                    oss << "{"
                        << "\"event_type\":\"status\","
                        << "\"tobii_initialized\":" << (g_app_state.device_connected ? "true" : "false") << ","
                        << "\"streaming\":" << (streaming ? "true" : "false") << ","
                        << "\"running\":" << (g_app_state.running ? "true" : "false") << ","
                        << "\"device_connected\":" << (g_app_state.device_connected ? "true" : "false") << ","
                        << "\"gaze_point\":" << (g_app_state.streams.gaze_point ? "true" : "false")
                        << "}";

                    conn.send_text(oss.str());
                }
            });

    std::cout << "[web] Crow server about to run" << std::endl;
    std::cout << "[web] HTTP             : http://127.0.0.1:9001" << std::endl;
    std::cout << "[web] Ping             : http://127.0.0.1:9001/ping" << std::endl;
    std::cout << "[web] Status           : http://127.0.0.1:9001/api/status" << std::endl;
    std::cout << "[web] Latest           : http://127.0.0.1:9001/api/latest" << std::endl;
    std::cout << "[web] Experiment Start : http://127.0.0.1:9001/api/experiment/start" << std::endl;
    std::cout << "[web] Experiment Stop  : http://127.0.0.1:9001/api/experiment/stop" << std::endl;
    std::cout << "[web] Stage Start      : http://127.0.0.1:9001/api/stage/start" << std::endl;
    std::cout << "[web] Stage Stop       : http://127.0.0.1:9001/api/stage/stop" << std::endl;
    std::cout << "[web] WS               : ws://127.0.0.1:9001/ws" << std::endl;

    app.port(9001).multithreaded().run();

    std::cout << "[web] app.run() returned" << std::endl;
}