#pragma once
#include "esp_err.h"
esp_err_t information_open(void (*on_close)(void));
