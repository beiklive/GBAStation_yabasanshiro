#include "UiFont.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../nanovg/nanovg.h"

#if defined(NX)
#include <switch.h>
#endif

// Embedded Latin fallback glyphs (see NvgUi.cpp / desktop UiGL.cpp for where
// these arrays are defined).
extern "C" {
extern unsigned char Roboto_Regular_ttf[];
extern unsigned int Roboto_Regular_ttf_len;
extern unsigned char Roboto_Bold_ttf[];
extern unsigned int Roboto_Bold_ttf_len;
}

namespace GbBoot
{
namespace
{
std::string JoinPath(const char* dir, const char* file)
{
  std::string path = dir;
  if (!path.empty() && path.back() != '/')
    path.push_back('/');
  path += file;
  return path;
}

int CreateFontFromFile(NVGcontext* vg, const char* name, const char* path)
{
  FILE* file = fopen(path, "rb");
  if (!file)
    return -1;
  fseek(file, 0, SEEK_END);
  const long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (size <= 0)
  {
    fclose(file);
    return -1;
  }
  unsigned char* buffer = static_cast<unsigned char*>(malloc(static_cast<size_t>(size)));
  if (!buffer)
  {
    fclose(file);
    return -1;
  }
  if (fread(buffer, 1, static_cast<size_t>(size), file) != static_cast<size_t>(size))
  {
    free(buffer);
    fclose(file);
    return -1;
  }
  fclose(file);
  const int font = nvgCreateFontMem(vg, name, buffer, static_cast<int>(size), 1);
  if (font == -1)
    free(buffer);
  return font;
}

int CreateFontMem(NVGcontext* vg, const char* name, unsigned char* data, size_t size,
                  int copy)
{
  if (!data || size == 0)
    return -1;
  if (copy)
  {
    unsigned char* owned = static_cast<unsigned char*>(malloc(size));
    if (!owned)
      return -1;
    memcpy(owned, data, size);
    return nvgCreateFontMem(vg, name, owned, static_cast<int>(size), 1);
  }
  return nvgCreateFontMem(vg, name, data, static_cast<int>(size), 0);
}

#if defined(NX)
int LoadSharedFont(NVGcontext* vg, const char* name, PlSharedFontType type)
{
  int font = -1;
  if (R_SUCCEEDED(plInitialize(PlServiceType_User)))
  {
    PlFontData data = {0};
    if (R_SUCCEEDED(plGetSharedFontByType(&data, type)) && data.address && data.size > 0)
    {
      font = CreateFontMem(vg, name, static_cast<unsigned char*>(data.address),
                           data.size, /*copy=*/1);
    }
    plExit();
  }
  return font;
}
#endif
}  // namespace

bool UiFontsLoad(NVGcontext* vg, UiFontSet& fonts, const char* font_dir)
{
  fonts = UiFontSet{};

  // ---- text ----
#if defined(NX)
  fonts.text = LoadSharedFont(vg, "text", PlSharedFontType_Standard);
#else
  if (font_dir && font_dir[0])
  {
    const std::string path = JoinPath(font_dir, "font.ttf");
    fonts.text = CreateFontFromFile(vg, "text", path.c_str());
  }
#endif
  if (fonts.text == -1)
  {
    // Latin-only fallback keeps ASCII browsing working everywhere.
    fonts.text = CreateFontMem(vg, "text", Roboto_Regular_ttf, Roboto_Regular_ttf_len, 0);
  }
  if (fonts.text == -1)
    return false;

  fonts.text_bold = fonts.text;
  // A CJK text font has no separate bold face; emphasis uses size/color.

  // ---- Nintendo switch icons ----
#if defined(NX)
  fonts.icons_switch = LoadSharedFont(vg, "switch", PlSharedFontType_NintendoExt);
#else
  if (font_dir && font_dir[0])
  {
    const std::string path = JoinPath(font_dir, "switch_icons.ttf");
    fonts.icons_switch = CreateFontFromFile(vg, "switch", path.c_str());
  }
#endif

  // ---- material icons ----
#if defined(NX)
  fonts.icons_material = CreateFontFromFile(vg, "material", "romfs:/font/MaterialIcons-Regular.ttf");
#else
  if (font_dir && font_dir[0])
  {
    const std::string path = JoinPath(font_dir, "MaterialIcons-Regular.ttf");
    fonts.icons_material = CreateFontFromFile(vg, "material", path.c_str());
  }
#endif

  return fonts.ok();
}
}
