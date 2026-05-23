#ifndef _CONNECT_H
#define _CONNECT_H

#include "global.h"
#include <assert.h>
#include <iostream>
#include <stdio.h>
#include <string.h>

void url_receiver(char const* url, void* user_data);
char* get_device(tobii_api_t* api);
tobii_api_t* api_create();
tobii_device_t* device_create(tobii_api_t* api, char* url);
void close_connect(tobii_device_t* device, tobii_api_t* api);

#endif