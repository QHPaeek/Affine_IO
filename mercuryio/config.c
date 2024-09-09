#include <windows.h>

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "config.h"

static const int mercury_io_default_cells[] = {
    '1','1','1','2','2','2','3','3','3','4','4','4','5','5','5','6','6','6','7','7','7','8','8','8','9','9','9','0','0','0',
    '1','1','1','2','2','2','3','3','3','4','4','4','5','5','5','6','6','6','7','7','7','8','8','8','9','9','9','0','0','0',
    'Q','Q','Q','W','W','W','E','E','E','R','R','R','T','T','T','Y','Y','Y','U','U','U','I','I','I','O','O','O','P','P','P',
    'Q','Q','Q','W','W','W','E','E','E','R','R','R','T','T','T','Y','Y','Y','U','U','U','I','I','I','O','O','O','P','P','P',
    'A','A','A','S','S','S','D','D','D','F','F','F','G','G','G','H','H','H','J','J','J','K','K','K','L','L','L',VK_OEM_1,VK_OEM_1,VK_OEM_1,    
    'A','A','A','S','S','S','D','D','D','F','F','F','G','G','G','H','H','H','J','J','J','K','K','K','L','L','L',VK_OEM_1,VK_OEM_1,VK_OEM_1,    
    'Z','Z','Z','X','X','X','C','C','C','V','V','V','B','B','B','N','N','N','M','M','M',VK_OEM_COMMA,VK_OEM_COMMA,VK_OEM_COMMA,VK_OEM_PERIOD,VK_OEM_PERIOD,VK_OEM_PERIOD,VK_OEM_2,VK_OEM_2,VK_OEM_2,   
    'Z','Z','Z','X','X','X','C','C','C','V','V','V','B','B','B','N','N','N','M','M','M',VK_OEM_COMMA,VK_OEM_COMMA,VK_OEM_COMMA,VK_OEM_PERIOD,VK_OEM_PERIOD,VK_OEM_PERIOD,VK_OEM_2,VK_OEM_2,VK_OEM_2,
};

void mercury_io_config_load(
        struct mercury_io_config *cfg,
        const wchar_t *filename)
{
    wchar_t key[240];
    int i;

    assert(cfg != NULL);
    assert(filename != NULL);

    cfg->vk_test = GetPrivateProfileIntW(L"io4", L"test", VK_F1, filename);
    cfg->vk_service = GetPrivateProfileIntW(L"io4", L"service", VK_F2, filename);
    cfg->vk_coin = GetPrivateProfileIntW(L"io4", L"coin", VK_F3, filename);
    cfg->vk_vol_up = GetPrivateProfileIntW(L"io4", L"volup", VK_UP, filename);
    cfg->vk_vol_down = GetPrivateProfileIntW(L"io4", L"voldown", VK_DOWN, filename);

    for (i = 0 ; i < 240 ; i++) {
        swprintf_s(key, _countof(key), L"cell%i", i + 1);
        cfg->vk_cell[i] = GetPrivateProfileIntW(
                L"touch",
                key,
                mercury_io_default_cells[i],
                filename);
    }
}
