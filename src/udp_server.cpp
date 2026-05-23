#include "udp_server.h"
#include <cstdlib>

void init_udp_server()
{
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        std::exit(1);
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
    {
        std::cerr << "UDP socket create failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        std::exit(1);
    }

    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(1234);
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (SOCKADDR*)&servAddr, sizeof(servAddr)) == SOCKET_ERROR)
    {
        std::cerr << "UDP bind failed: " << WSAGetLastError() << std::endl;
        closesocket(sock);
        sock = INVALID_SOCKET;
        WSACleanup();
        std::exit(1);
    }

    DWORD timeout_ms = 100;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR)
    {
        std::cerr << "setsockopt SO_RCVTIMEO failed: " << WSAGetLastError() << std::endl;
    }

    nSize = sizeof(clientAddr);

    std::cout << "UDP server listening on port 1234" << std::endl;
}

void close_udp_server()
{
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    WSACleanup();
}