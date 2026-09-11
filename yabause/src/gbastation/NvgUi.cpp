#include "NvgUi.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../nanovg/nanovg.h"

#include "UiIcons.h"

namespace GbBoot
{
namespace
{
//---------------------------------------------------------------------------
// Flat VS Code Dark+ palette
//---------------------------------------------------------------------------
struct RGBA
{
  float r, g, b, a;
};

NVGcolor Col(const RGBA& c)
{
  return nvgRGBA(static_cast<unsigned char>(c.r * 255.0f),
                 static_cast<unsigned char>(c.g * 255.0f),
                 static_cast<unsigned char>(c.b * 255.0f),
                 static_cast<unsigned char>(c.a * 255.0f));
}

const RGBA kBackground = {0.118f, 0.118f, 0.118f, 1.0f};   // #1E1E1E
const RGBA kPanel = {0.145f, 0.145f, 0.149f, 1.0f};        // #252526
const RGBA kBorder = {0.235f, 0.235f, 0.235f, 1.0f};       // #3C3C3C
const RGBA kSelection = {0.016f, 0.224f, 0.369f, 1.0f};    // #04395E
const RGBA kButtonBlue = {0.055f, 0.39f, 0.612f, 1.0f};    // #0E639C
const RGBA kAccent = {0.0f, 0.48f, 0.8f, 1.0f};            // #007ACC
const RGBA kText = {0.83f, 0.83f, 0.83f, 1.0f};            // #D4D4D4
const RGBA kTextSub = {0.61f, 0.61f, 0.61f, 1.0f};
const RGBA kTextWeak = {0.45f, 0.45f, 0.45f, 1.0f};
const RGBA kTextOnAccent = {1.0f, 1.0f, 1.0f, 1.0f};
const RGBA kDirColor = {0.337f, 0.612f, 0.839f, 1.0f};     // #569CD6
const RGBA kGameColor = {0.29f, 0.788f, 0.69f, 1.0f};      // #4EC9B0
const RGBA kFileColor = {0.83f, 0.83f, 0.83f, 1.0f};
const RGBA kError = {0.957f, 0.529f, 0.443f, 1.0f};        // #F48771
const RGBA kIconOnSelected = {1.0f, 1.0f, 1.0f, 1.0f};

//---------------------------------------------------------------------------
// Layout
//---------------------------------------------------------------------------
struct Theme
{
  float scale;
  float margin;
  float pad;
  float path_h;
  float row_h;
  float footer_h;
  float path_size;
  float item_size;
  float footer_size;
  float status_size;
};

Theme MakeTheme(int fb_h)
{
  Theme t;
  t.scale = fb_h / 720.0f;
  if (t.scale < 0.5f)
    t.scale = 0.5f;
  const float s = t.scale;
  t.margin = 24.0f * s;
  t.pad = 10.0f * s;
  t.path_h = 52.0f * s;
  t.row_h = 46.0f * s;
  t.footer_h = 46.0f * s;
  t.path_size = 21.0f * s;
  t.item_size = 22.0f * s;
  t.footer_size = 17.0f * s;
  t.status_size = 17.0f * s;
  return t;
}

//---------------------------------------------------------------------------
// Text helpers (UTF-8 safe)
//---------------------------------------------------------------------------
size_t Utf8Chars(const std::string& s)
{
  size_t count = 0;
  for (size_t i = 0; i < s.size();)
  {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80)
      i += 1;
    else if ((c >> 5) == 0x6)
      i += 2;
    else if ((c >> 4) == 0xE)
      i += 3;
    else if ((c >> 3) == 0x1E)
      i += 4;
    else
      i += 1;
    ++count;
  }
  return count;
}

std::string Utf8Prefix(const std::string& s, size_t chars)
{
  size_t count = 0;
  size_t i = 0;
  for (; i < s.size() && count < chars;)
  {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80)
      i += 1;
    else if ((c >> 5) == 0x6)
      i += 2;
    else if ((c >> 4) == 0xE)
      i += 3;
    else if ((c >> 3) == 0x1E)
      i += 4;
    else
      i += 1;
    ++count;
  }
  return s.substr(0, i);
}

void AppendUtf8(std::string& out, uint32_t cp)
{
  if (cp < 0x80)
  {
    out.push_back(static_cast<char>(cp));
  }
  else if (cp < 0x800)
  {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  else if (cp < 0x10000)
  {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  else
  {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

void Text(NVGcontext* vg, int font, float size, float x, float y, int align,
          const RGBA& color, const char* text)
{
  nvgFontFaceId(vg, font);
  nvgFontSize(vg, size);
  nvgTextAlign(vg, align);
  nvgFillColor(vg, Col(color));
  nvgText(vg, x, y, text, nullptr);
}

void TextEllipsis(NVGcontext* vg, int font, float size, float x, float y, int align,
                  const RGBA& color, const std::string& text, float max_w)
{
  nvgFontFaceId(vg, font);
  nvgFontSize(vg, size);
  nvgTextAlign(vg, align);
  nvgFillColor(vg, Col(color));

  float bounds[4];
  nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
  const float width = bounds[2] - bounds[0];
  if (width <= max_w)
  {
    nvgText(vg, x, y, text.c_str(), nullptr);
    return;
  }

  const size_t total = Utf8Chars(text);
  size_t lo = 0;
  size_t hi = total > 2 ? total - 2 : 0;
  while (lo < hi)
  {
    const size_t mid = (lo + hi + 1) / 2;
    const std::string prefix = Utf8Prefix(text, mid);
    nvgTextBounds(vg, 0, 0, prefix.c_str(), nullptr, bounds);
    const float w = bounds[2] - bounds[0];
    if (w <= max_w - size * 0.7f)
      lo = mid;
    else
      hi = mid - 1;
  }
  const std::string result = Utf8Prefix(text, lo) + "\xE2\x80\xA6";
  nvgText(vg, x, y, result.c_str(), nullptr);
}

float TextWidth(NVGcontext* vg, int font, float size, const char* text)
{
  nvgFontFaceId(vg, font);
  nvgFontSize(vg, size);
  nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
  float bounds[4];
  nvgTextBounds(vg, 0, 0, text, nullptr, bounds);
  return bounds[2] - bounds[0];
}

//---------------------------------------------------------------------------
// Icon glyph drawing (single codepoint from an icon font)
//---------------------------------------------------------------------------
void Glyph(NVGcontext* vg, int font, uint32_t codepoint, float cx, float cy, float size,
           const RGBA& color)
{
  if (font < 0)
    return;
  std::string glyph;
  AppendUtf8(glyph, codepoint);
  nvgFontFaceId(vg, font);
  nvgFontSize(vg, size);
  nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
  nvgFillColor(vg, Col(color));
  nvgText(vg, cx, cy, glyph.c_str(), nullptr);
}

void RoundedBox(NVGcontext* vg, float x, float y, float w, float h, float r,
                const RGBA& color)
{
  nvgBeginPath(vg);
  nvgRoundedRect(vg, x, y, w, h, r);
  nvgFillColor(vg, Col(color));
  nvgFill(vg);
}

//---------------------------------------------------------------------------
// Pieces
//---------------------------------------------------------------------------
void DrawPathBar(NVGcontext* vg, const Theme& t, const UiFontSet& fonts, int fb_w,
                 const std::string& dir)
{
  RoundedBox(vg, 0, 0, fb_w, t.path_h, 0, kPanel);

  const float cy = t.path_h / 2.0f;
  const float icon_size = t.path_size * 1.35f;
  Glyph(vg, fonts.icons_material, Material::kFolder, t.margin + icon_size * 0.5f, cy,
        icon_size, kDirColor);
  const float text_x = t.margin + icon_size + t.pad;
  const float max_w = fb_w - text_x - t.margin;

  TextEllipsis(vg, fonts.text, t.path_size, text_x, cy, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
               kTextSub, dir, max_w);
}

void DrawRow(NVGcontext* vg, const Theme& t, const UiFontSet& fonts, int fb_w,
             const DirEntry& item, size_t index, size_t cursor, float y)
{
  const float row_h = t.row_h;
  const float mid = y + row_h / 2.0f;
  const bool selected = (index == cursor);

  if (selected)
  {
    RoundedBox(vg, t.margin, y + 3.0f * t.scale, fb_w - 2 * t.margin,
               row_h - 6.0f * t.scale, 2.0f * t.scale, kSelection);
  }

  const float icon_size = t.item_size * 1.15f;
  const float icon_cx = t.margin + icon_size * 0.5f + t.pad;

  // Choose glyph + colour.
  uint32_t icon_cp = Material::kFile;
  RGBA icon_color = item.is_game ? kGameColor : kFileColor;
  if (item.is_parent)
  {
    icon_cp = Material::kArrowUp;
    icon_color = kAccent;
  }
  else if (item.is_dir)
  {
    icon_cp = Material::kFolder;
    icon_color = kDirColor;
  }
  else if (item.is_game)
  {
    icon_cp = Material::kGame;
    icon_color = kGameColor;
  }
  if (selected && item.is_parent)
    icon_color = kIconOnSelected;
  if (selected && !item.is_parent)
    icon_color = kIconOnSelected;

  Glyph(vg, fonts.icons_material, icon_cp, icon_cx, mid, icon_size, icon_color);

  const std::string label =
      item.is_parent ? "返回上一级" : item.name;
  const RGBA text_color =
      selected ? kTextOnAccent : (item.is_dir ? kDirColor : (item.is_game ? kGameColor : kFileColor));

  const float text_x = icon_cx + icon_size * 0.5f + t.pad;
  const float max_w = fb_w - text_x - t.margin - 8.0f * t.scale;
  TextEllipsis(vg, fonts.text, t.item_size, text_x, mid, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
               text_color, label, max_w);
}

void DrawStatus(NVGcontext* vg, const Theme& t, const UiFontSet& fonts, int fb_w,
                const char* status)
{
  if (!status || !status[0])
    return;
  const float y = t.path_h + t.status_size / 2.0f;
  Text(vg, fonts.text, t.status_size, fb_w / 2.0f, y, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
       kError, status);
}

void DrawCenteredMessage(NVGcontext* vg, const Theme& t, const UiFontSet& fonts, int fb_w,
                         int fb_h, const std::string& title, const std::string& sub)
{
  const float cx = fb_w / 2.0f;
  const float cy = (t.path_h + fb_h - t.footer_h) / 2.0f;
  Glyph(vg, fonts.icons_material, Material::kFolder, cx, cy - 56.0f * t.scale,
        46.0f * t.scale, kDirColor);
  Text(vg, fonts.text, t.item_size + 2.0f, cx, cy, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, kText,
       title.c_str());
  Text(vg, fonts.text, t.status_size, cx, cy + 30.0f * t.scale,
       NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, kTextSub, sub.c_str());
}
}  // namespace

void NvgUiDrawBrowser(NVGcontext* vg, const DirBrowser& browser, const UiFontSet& fonts,
                      int fb_w, int fb_h, const char* status)
{
  const Theme t = MakeTheme(fb_h);
  nvgBeginFrame(vg, fb_w, fb_h, 1.0f);

  nvgFillColor(vg, Col(kBackground));
  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, fb_w, fb_h);
  nvgFill(vg);

  DrawPathBar(vg, t, fonts, fb_w, browser.Directory());

  // Status line (error / notice) below the path when present.
  float list_top = t.path_h;
  if (status && status[0])
  {
    DrawStatus(vg, t, fonts, fb_w, status);
    list_top += t.status_size + 6.0f * t.scale;
  }

  const float list_bottom = fb_h - t.footer_h;
  const float list_height = list_bottom - list_top;
  const size_t rows_visible =
      list_height > t.row_h ? static_cast<size_t>(list_height / t.row_h) : 0;

  const size_t total = browser.Entries().size();
  const size_t cursor = browser.Cursor();

  static std::string last_dir;
  static size_t scroll = 0;
  if (last_dir != browser.Directory())
  {
    last_dir = browser.Directory();
    scroll = 0;
  }
  if (rows_visible == 0)
  {
    // footer only
  }
  else
  {
    if (cursor < scroll)
      scroll = cursor;
    else if (cursor >= scroll + rows_visible)
      scroll = cursor - rows_visible + 1;
    for (size_t i = 0; i < rows_visible && scroll + i < total; ++i)
    {
      const float y = list_top + static_cast<float>(i) * t.row_h;
      DrawRow(vg, t, fonts, fb_w, browser.Entries()[scroll + i], scroll + i, cursor, y);
    }
  }

  // Footer.
  RoundedBox(vg, 0, fb_h - t.footer_h, fb_w, t.footer_h, 0, kPanel);
  const float y = fb_h - t.footer_h / 2.0f;

  // Key hints: [icon] text  [icon] text ...
  struct HintPart
  {
    int font;
    std::string text;
    RGBA color;
  };
  std::vector<HintPart> parts;
  auto push_glyph = [&](int font, uint32_t cp) {
    HintPart p;
    p.font = font;
    AppendUtf8(p.text, cp);
    p.color = kTextSub;
    parts.push_back(p);
  };
  auto push_text = [&](const char* s, const RGBA& color) {
    HintPart p;
    p.font = fonts.text;
    p.text = s;
    p.color = color;
    parts.push_back(p);
  };
  auto push_key = [&](uint32_t codepoint, const char* fallback) {
    if (fonts.icons_switch >= 0)
      push_glyph(fonts.icons_switch, codepoint);
    else
      push_text(fallback, kTextSub);
  };

  push_key(SwitchIcon::kUp, "^");
  push_key(SwitchIcon::kDown, "v");
  push_text("选择 ", kTextSub);
  push_key(SwitchIcon::kA, "A");
  push_text(" 打开/启动 ", kTextSub);
  push_key(SwitchIcon::kB, "B");
  push_text(" 返回上级 ", kTextSub);
  push_key(SwitchIcon::kY, "Y");
  push_text(" 仅游戏 ", kTextSub);
  push_key(SwitchIcon::kX, "X");
  push_text(" 排序 ", kTextSub);
  push_key(SwitchIcon::kPlus, "+");
  push_text(" 退出", kTextSub);

  float x = t.margin;
  for (const HintPart& part : parts)
  {
    const float width = TextWidth(vg, part.font, t.footer_size, part.text.c_str());
    Text(vg, part.font, t.footer_size, x, y, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, part.color,
         part.text.c_str());
    x += width + (part.font == fonts.text ? 2.0f * t.scale : 4.0f * t.scale);
  }

  // Centered messages: empty folder, or unopenable directory.
  if (total == 0)
  {
    if (browser.CanGoParent() == false && !(status && status[0]))
    {
      DrawCenteredMessage(vg, t, fonts, fb_w, fb_h, "此文件夹为空",
                          ".cue / .iso / .chd / .ccd / .mds");
    }
    else if (status && status[0])
    {
      DrawCenteredMessage(vg, t, fonts, fb_w, fb_h, status,
                          browser.Directory());
    }
  }

  nvgEndFrame(vg);
}

void NvgUiDrawLaunchOverlay(NVGcontext* vg, const UiFontSet& fonts, int fb_w, int fb_h,
                            const char* game_name)
{
  const Theme t = MakeTheme(fb_h);
  nvgBeginFrame(vg, fb_w, fb_h, 1.0f);

  const float cx = fb_w / 2.0f;
  const float cy = fb_h / 2.0f;

  nvgFillColor(vg, nvgRGBA(0, 0, 0, 210));
  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, fb_w, fb_h);
  nvgFill(vg);

  Glyph(vg, fonts.icons_material, Material::kGame, cx, cy - 58.0f * t.scale,
        52.0f * t.scale, kGameColor);

  Text(vg, fonts.text, t.item_size + 3.0f, cx, cy, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, kText,
       "正在启动 Saturn");
  Text(vg, fonts.text, t.item_size, cx, cy + 38.0f * t.scale,
       NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, kTextSub, game_name ? game_name : "");

  nvgEndFrame(vg);
}
}
