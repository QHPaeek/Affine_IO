#pragma once

#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>

struct mercury_io_config {
    uint8_t vk_test;
    uint8_t vk_service;
    uint8_t vk_coin;
    uint8_t vk_vol_up;
    uint8_t vk_vol_down;
    uint8_t vk_cell[240];
};

void mercury_io_config_load(
        struct mercury_io_config *cfg,
        const wchar_t *filename);
