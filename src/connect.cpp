#include "connect.h"
#include "save_data.h"
#include <cstdlib>

void url_receiver(char const* input_url, void* user_data)
{
    char* buffer = (char*)user_data;

    if (*buffer != '\0')
        return;

    if (strlen(input_url) < 256)
        strcpy_s(buffer, 256, input_url);
}

char* get_device(tobii_api_t* api)
{
    if (api == NULL)
    {
        std::cerr << "[connect] get_device failed: api is null" << std::endl;
        return nullptr;
    }

    url[0] = '\0';

    result = tobii_enumerate_local_device_urls(api, url_receiver, url);
    if (result != TOBII_ERROR_NO_ERROR)
    {
        std::cerr << "[connect] tobii_enumerate_local_device_urls failed: " << result << std::endl;
        return nullptr;
    }

    if (*url == '\0')
    {
        std::cerr << "[connect] Warning: No device found" << std::endl;
        return nullptr;
    }

    std::cout << "[connect] Device URL: " << url << std::endl;
    return url;
}

tobii_api_t* api_create()
{
    result = tobii_api_create(&api, NULL, NULL);
    if (result != TOBII_ERROR_NO_ERROR)
    {
        std::cerr << "[connect] tobii_api_create failed: " << result << std::endl;
        return nullptr;
    }
    return api;
}

tobii_device_t* device_create(tobii_api_t* api, char* url)
{
    if (api == NULL || url == nullptr)
        return nullptr;

    result = tobii_device_create(api, url, TOBII_FIELD_OF_USE_INTERACTIVE, &device);
    if (result != TOBII_ERROR_NO_ERROR)
    {
        std::cerr << "[connect] tobii_device_create failed: " << result << std::endl;
        return nullptr;
    }

    return device;
}

void close_connect(tobii_device_t* input_device, tobii_api_t* input_api)
{
    if (input_device != NULL)
    {
        stop_listen(input_device);

        result = tobii_device_destroy(input_device);
        if (result != TOBII_ERROR_NO_ERROR)
        {
            std::cerr << "[connect] tobii_device_destroy failed: " << result << std::endl;
        }
    }

    if (input_api != NULL)
    {
        result = tobii_api_destroy(input_api);
        if (result != TOBII_ERROR_NO_ERROR)
        {
            std::cerr << "[connect] tobii_api_destroy failed: " << result << std::endl;
        }
    }

    device = NULL;
    api = NULL;
    url[0] = '\0';

    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        g_app_state.device_connected = false;
        g_app_state.running = false;
        g_app_state.recording = false;
        g_app_state.streams = StreamSubscriptionState{};
    }
}