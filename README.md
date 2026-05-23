# Tobii Eye Tracker 5 eye tracking data streams capture

This project is designed to collect Tobii eye-tracking data during browser-based experimental tasks.

The project adopts a **C++ backend + browser-based JavaScript frontend** architecture:
```text
Tobii Eye Tracker
        ↓
C++ backend program
        ↓ WebSocket / HTTP
browser-based experimental interface
```

## Function
**（1）Connects to a Tobii eye tracker**  

**（2）Subscribes to multiple Tobii data streams:**  

_gaze_point_, _gaze_origin_, _eye_position_normalized_, _head_pose_, _user_presence_, _user_position_guide_, _notifications_  

**（3）Streams eye-tracking data to the browser in real time via WebSocket**  

**（4）Supports jsPsych-based web experiments**  

## Project Structure
```
project/
├── src/
│   ├── global.h
│   ├── connect.cpp / connect.h
│   ├── save_data.cpp / save_data.h
│   ├── shared_state.cpp / shared_state.h
│   ├── udp_server.cpp / udp_server.h
│   ├── web_server.cpp / web_server.h
│
├── tobii/
│   ├── tobii.h
│   ├── tobii_streams.h
│
├── experiment.js
├── index.html
├── README.md
```

## 环境要求
Windows
Visual Studio
Tobii Eye Tracker
Tobii SDK / Tobii headers
C++17 或更高版本
Crow HTTP/WebSocket library
浏览器：Chrome 或 Edge 推荐

## 后端启动方式
使用 Visual Studio 打开 C++ 项目。
确认 Tobii 眼动仪已连接并完成校准。
编译并运行后端程序。
后端默认启动以下服务：
```
HTTP:      http://127.0.0.1:9001
WebSocket: ws://127.0.0.1:9001/ws
Status:   http://127.0.0.1:9001/api/status
Latest:   http://127.0.0.1:9001/api/latest
```

## 前端启用方式
启动后端程序。
用浏览器打开实验网页。
网页会连接：
```
ws://127.0.0.1:9001/ws
```
实验开始后，前端通过 API 控制数据记录：
```
POST /api/experiment/start
POST /api/experiment/stop
POST /api/stage/start
POST /api/stage/stop
```
## 数据保存
主要输出包括：
```
full_stream.jsonl
stage_xxx.jsonl
subject_gaze_data.json
subject_gaze_xyt_segmented.json
subject_tobii_fullstream_observation_to_main_end.json
```

## License

For research use only.
