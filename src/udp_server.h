#ifndef _UDP_SERVER_H
#define _UDP_SERVER_H

#include "global.h"
#include <cstring>
#include <iostream>
#include <tchar.h>
#include <winsock2.h>
#include <ws2tcpip.h>

void init_udp_server();
void close_udp_server();

#endif