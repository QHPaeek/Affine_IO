#pragma once

#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>

struct mai2_io_config {
    uint8_t vk_test;
    uint8_t vk_service;
    uint8_t vk_coin;
    bool vk_btn_enable;
    uint8_t vk_1p_btn[9];
    uint8_t vk_2p_btn[9];
    bool debug_input_1p;
    bool debug_input_2p;
    uint8_t vk_1p_touch[34];
    uint8_t vk_2p_touch[34];
};

void mai2_io_config_load(
        struct mai2_io_config *cfg,
        const wchar_t *filename);
