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
│   ├── global.cpp /global.h
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
├── main.cpp
├── README.md
```

## Environmental Requirements

Windows  

Visual Studio  

Tobii Eye Tracker  

Tobii SDK / Tobii headers  

C++17 or above  

Crow HTTP/WebSocket library  

Recommended browsers: Chrome, Edge  

## Backend Startup Method

Open the C++ project with Visual Studio  

Ensure the Tobii eye tracker is connected and calibrated  

Compile and run the backend program  

The following services will launch by default:  

```
HTTP:      http://127.0.0.1:9001
WebSocket: ws://127.0.0.1:9001/ws
Status:   http://127.0.0.1:9001/api/status
Latest:   http://127.0.0.1:9001/api/latest
```

## Frontend Activation Method

Start the backend program  

Open the experimental webpage via browser  

The webpage will connect to:  

```
ws://127.0.0.1:9001/ws
```

After the experiment starts, the frontend controls data recording via API 
```
POST /api/experiment/start
POST /api/experiment/stop
POST /api/stage/start
POST /api/stage/stop
```
## Data Saving

Main outputs include:  

```
full_stream.jsonl
stage_xxx.jsonl
subject_gaze_data.json
subject_gaze_xyt_segmented.json
subject_tobii_fullstream_observation_to_main_end.json
```

## License

For research use only.
