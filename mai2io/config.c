#include <windows.h>

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "config.h"

static const int mai2_io_1p_default[] = {'W', 'E', 'D', 'C', 'X', 'Z', 'A', 'Q'};
static const int mai2_io_2p_default[] = {0x68, 0x69, 0x66, 0x63, 0x62, 0x61, 0x64, 0x67};

void mai2_io_config_load(
        struct mai2_io_config *cfg,
        const wchar_t *filename)
{
    wchar_t key[240];
    int i;

    assert(cfg != NULL);
    assert(filename != NULL);

    cfg->vk_test = GetPrivateProfileIntW(L"io4", L"test", VK_DELETE, filename);
    cfg->vk_service = GetPrivateProfileIntW(L"io4", L"service", VK_END, filename);
    cfg->vk_coin = GetPrivateProfileIntW(L"io4", L"coin", VK_INSERT, filename);
    cfg->vk_p1_start = GetPrivateProfileIntW(L"io4", L"p1_start", '1', filename);
    cfg->vk_p2_start = GetPrivateProfileIntW(L"io4", L"p2_start", '2', filename);

}
