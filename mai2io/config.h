#pragma once

#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>

struct mai2_io_config {
    uint8_t vk_test;
    uint8_t vk_service;
    uint8_t vk_coin;
    uint8_t vk_p1_start;
    uint8_t vk_p2_start;
    uint8_t vk_p1_btn[8];
    uint8_t vk_p2_btn[8];
};

void mai2_io_config_load(
        struct mai2_io_config *cfg,
        const wchar_t *filename);
