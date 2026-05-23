#pragma once
#include <string>

void start_web_server();
void broadcast_stream_ws(const std::string& message);
int get_ws_client_count();