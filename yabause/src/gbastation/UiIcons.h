#pragma once

#include <cstdint>

// Icon codepoints used by the boot page UI.
//
// "material" glyphs are drawn from the Material Icons font
// (res/font/MaterialIcons-Regular.ttf).  "switch" glyphs are the Nintendo
// shared-font extended characters (NintendoExt, PlSharedFontType_NintendoExt
// on Switch, res/font/switch_icons.ttf elsewhere) and match borealis'
// Hint::getKeyIcon() mapping.
namespace GbBoot
{
namespace Material
{
constexpr uint32_t kFolder = 0xE2C7;
constexpr uint32_t kFolderOpen = 0xE2C8;
constexpr uint32_t kFile = 0xE24D;          // insert_drive_file
constexpr uint32_t kGame = 0xE338;          // videogame_asset
constexpr uint32_t kArrowUp = 0xE5D8;
constexpr uint32_t kArrowDown = 0xE5DB;
constexpr uint32_t kArrowBack = 0xE317;
}  // namespace Material

namespace SwitchIcon
{
constexpr uint32_t kA = 0xE0E0;
constexpr uint32_t kB = 0xE0E1;
constexpr uint32_t kX = 0xE0E2;
constexpr uint32_t kY = 0xE0E3;
constexpr uint32_t kL = 0xE0E4;
constexpr uint32_t kR = 0xE0E5;
constexpr uint32_t kZL = 0xE0E6;
constexpr uint32_t kZR = 0xE0E7;
constexpr uint32_t kUp = 0xE0EB;
constexpr uint32_t kDown = 0xE0EC;
constexpr uint32_t kLeft = 0xE0ED;
constexpr uint32_t kRight = 0xE0EE;
constexpr uint32_t kPlus = 0xE0EF;
constexpr uint32_t kMinus = 0xE0F0;
constexpr uint32_t kLStick = 0xE104;
constexpr uint32_t kRStick = 0xE105;
constexpr uint32_t kFallback = 0xE152;
}  // namespace SwitchIcon
}
