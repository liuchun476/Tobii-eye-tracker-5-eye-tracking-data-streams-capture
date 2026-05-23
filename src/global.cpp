#include "global.h"

// Tobii
char url[256] = { 0 };
tobii_api_t* api = NULL;
tobii_device_t* device = NULL;
tobii_error_t result;

// UDP
WSADATA wsaData;
char buffer[BUF_SIZE];
SOCKET sock = INVALID_SOCKET;
sockaddr_in servAddr;
SOCKADDR clientAddr;
int nSize = 0;