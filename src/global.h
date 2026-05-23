#ifndef _GLOBAL_H
#define _GLOBAL_H

#include "../tobii/tobii.h"
#include "../tobii/tobii_streams.h"
#include <winsock2.h>
#include <ws2tcpip.h>

#define BUF_SIZE 101
#define SUCCESS_TIME 1000

#pragma comment(lib, "ws2_32.lib")

// Tobii
extern char url[256];
extern tobii_api_t* api;
extern tobii_device_t* device;
extern tobii_error_t result;

// UDP server
extern WSADATA wsaData;
extern char buffer[BUF_SIZE];
extern SOCKET sock;
extern sockaddr_in servAddr;
extern SOCKADDR clientAddr;
extern int nSize;

#endif