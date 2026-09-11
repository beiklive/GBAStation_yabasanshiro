#pragma once

struct NVGcontext;

// UI fonts used by the boot page.
//
//   text            CJK-capable text face.
//                   Switch : HOS shared font (PlSharedFontType_Standard)
//                   other  : font.ttf from the resource font directory
//   icons_switch    Nintendo extended glyphs (A/B/X/Y/ZL/...).
//                   Switch : HOS NintendoExt shared font
//                   other  : switch_icons.ttf from the resource dir
//   icons_material  Material Icons (folder/file/back/game).
//                   Switch : romfs:/font/MaterialIcons-Regular.ttf
//                   other  : MaterialIcons-Regular.ttf from the resource dir
namespace GbBoot
{
struct UiFontSet
{
  int text = -1;
  int text_bold = -1;  // fallback equals text when no bold face exists
  int icons_switch = -1;
  int icons_material = -1;

  bool ok() const { return text >= 0; }
};

// font_dir is only used on non-Switch platforms (a directory containing
// font.ttf, switch_icons.ttf and MaterialIcons-Regular.ttf).  On Switch the
// text/switch fonts come from the HOS shared fonts and material from romfs.
// Returns true when at least the text face could be loaded.
bool UiFontsLoad(NVGcontext* vg, UiFontSet& fonts, const char* font_dir);
}
