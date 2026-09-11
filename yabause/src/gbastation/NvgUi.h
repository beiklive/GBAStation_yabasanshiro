#pragma once

#include "Browser.h"
#include "Settings.h"
#include "UiFont.h"

struct NVGcontext;

// NanoVG rendering for the boot page.
//
// Layout (as requested):
//   top    = current directory path
//   list   = icon + name rows, first row is "return to parent" when a parent
//            directory exists
//   bottom = single line of key hints drawn with the Switch icon font
// Status (error/notice) is shown between path and list when present.
namespace GbBoot
{
void NvgUiDrawBrowser(NVGcontext* vg, const DirBrowser& browser, const UiFontSet& fonts,
                      int fb_w, int fb_h, const char* status);

// Full screen "launching a game" overlay; draw and swap once before booting.
void NvgUiDrawLaunchOverlay(NVGcontext* vg, const UiFontSet& fonts, int fb_w, int fb_h,
                            const char* game_name);

// Settings page: list of core settings with the current value on the right.
void NvgUiDrawSettings(NVGcontext* vg, const std::vector<SettingItem>& items,
                       const UiFontSet& fonts, int fb_w, int fb_h, int cursor, int scroll,
                       const char* status);
}
