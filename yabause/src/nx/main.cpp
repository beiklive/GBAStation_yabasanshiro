/*
        Copyright 2019 devMiyax(smiyaxdev@gmail.com)

This file is part of YabaSanshiro.

        YabaSanshiro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

YabaSanshiro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
along with YabaSanshiro; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include <exception>
#include <functional>
#include <string>  
#include <vector>
#include <fstream>
#include <map>
#include <cstdlib>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/resource.h>
#include <errno.h>
#include <pthread.h>

#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengles2.h>

#include <switch.h>
#include <switch/services/hid.h>
#include <time.h>
#include "yabause.h"
#include "../gbastation/YabLog.h"
#include "../gbastation/Browser.h"
#include "../gbastation/NvgUi.h"
#include "../gbastation/UiFont.h"
#include "../gbastation/UiInput.h"
#include "../gbastation/Settings.h"
#include "nanovg.h"
#define NANOVG_GLES3 1
#include "nanovg_gl.h"
extern "C" {
void PlayRecorder_proc(u32) {}
void PlayRecorder_setPlayMode(const char*, yabauseinit_struct*) {}
int YabNanosleep(u64 ns) { svcSleepThread(ns); return 0; }
}

extern "C" {
#include "../config.h"
#include "yabause.h"
#include "vdp2.h"
#include "scsp.h"
#include "vidsoft.h"
#include "vidogl.h"
#include "peripheral.h"
#include "persdljoy.h"
#include "m68kcore.h"
#include "sh2core.h"
#include "sh2int.h"
#include "cdbase.h"
#include "cs2.h"
#include "debug.h"
#include "sndal.h"
#include "sndsdl.h"
#include "osdcore.h"
#include "ygl.h"
//#include "libpng12/png.h"
}

static EGLDisplay s_display;
static EGLContext s_context;
static EGLSurface s_surface;

//#include "InputManager.h"
//#include "MenuScreen.h"
#define ENABLE_NXLINK
#ifndef ENABLE_NXLINK
#define TRACE(fmt,...) ((void)0)
#else
#include <unistd.h>
#define TRACE(fmt,...) printf("%s: " fmt "\n", __PRETTY_FUNCTION__, ## __VA_ARGS__)
static int s_nxlinkSock = -1;

static void initNxLink()
{
	if (R_FAILED(socketInitializeDefault()))
		return;

	s_nxlinkSock = nxlinkStdio();
	if (s_nxlinkSock >= 0)
		TRACE("printf output now goes to nxlink server");
	else
		socketExit();
}

static void deinitNxLink()
{
	if (s_nxlinkSock >= 0)
	{
		close(s_nxlinkSock);
		socketExit();
		s_nxlinkSock = -1;
	}
}

extern "C" {

void userAppInit()
{
	initNxLink();
}

void userAppExit()
{
	deinitNxLink();
}
}

#endif

extern "C" {
static char biospath[256] = "sdmc:/GBAStation/bios/saturn/saturn_bios.bin";
// Empty by default: with no ROM argument the built-in boot page is shown.
static char cdpath[256] = "";
//static char cdpath[256] = "/home/pigaming/RetroPie/roms/saturn/gd.cue";
//static char cdpath[256] = "/home/pigaming/RetroPie/roms/saturn/Virtua Fighter Kids (1996)(Sega)(JP).ccd";
static char buppath[256] = "./back.bin";

static std::map<std::string, std::string> LoadGbaConfig()
{
  std::map<std::string, std::string> values;
  std::ifstream in("sdmc:/GBAStation/config/config.cfg");
  std::string line;
  while (std::getline(in, line)) {
    const size_t hash = line.find('#'); if (hash != std::string::npos) line.resize(hash);
    const size_t eq = line.find('='); if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    while (!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
    while (!value.empty() && isspace((unsigned char)value.front())) value.erase(value.begin());
    while (!value.empty() && isspace((unsigned char)value.back())) value.pop_back();
    // GBAStation stores values as "<type>|<value>" (e.g. "s|path", "i|1",
    // "sa|a,b").  Strip the short alphabetic type tag so consumers see the
    // plain value.
    const size_t bar = value.find('|');
    if (bar != std::string::npos && bar > 0 && bar <= 2) {
      bool is_type = true;
      for (size_t i = 0; i < bar; ++i)
        if (!isalpha((unsigned char)value[i])) { is_type = false; break; }
      if (is_type) value.erase(0, bar + 1);
    }
    if (!key.empty()) values[key] = value;
  }
  return values;
}

static const char* ConfigValue(const std::map<std::string, std::string>& c,
                               const char* key, const char* fallback)
{
  const auto it = c.find(key); return it == c.end() ? fallback : it->second.c_str();
}

static bool ConfigBool(const std::map<std::string, std::string>& c,
                       const char* key, bool fallback)
{
  const auto it = c.find(key);
  if (it == c.end()) return fallback;
  const std::string& v = it->second;
  if (v == "1" || v == "true" || v == "on" || v == "yes" || v == "enabled") return true;
  if (v == "0" || v == "false" || v == "off" || v == "no" || v == "disabled") return false;
  return fallback;
}

static int ConfigInt(const std::map<std::string, std::string>& c,
                     const char* key, int fallback)
{
  const auto it = c.find(key);
  if (it == c.end() || it->second.empty()) return fallback;
  return atoi(it->second.c_str());
}

static std::string NormalizeSwitchPath(const char* path)
{
  if (!path) return std::string();
  if (!strncmp(path, "/GBAStation/", 12)) return std::string("sdmc:") + path;
  return std::string(path);
}
static char mpegpath[256] = "\0";
static char cartpath[256] = "\0";

#define LOG

M68K_struct * M68KCoreList[] = {
  &M68KDummy,
  #ifdef HAVE_C68K
  &M68KC68K,
  #endif
  #ifdef HAVE_Q68
  &M68KQ68,
  #endif
#ifdef HAVE_MUSASHI
  &M68KMusashi,
#endif
  NULL
};

SH2Interface_struct *SH2CoreList[] = {
  &SH2Interpreter,
  &SH2DebugInterpreter,
#ifdef SH2_DYNAREC
  &SH2Dynarec,
#endif
#if DYNAREC_DEVMIYAX
  &SH2Dyn,
  &SH2DynDebug,
#endif
  NULL
};

PerInterface_struct *PERCoreList[] = {
  &PERDummy,
  NULL
};

CDInterface *CDCoreList[] = {
  &DummyCD,
  &ISOCD,
  NULL
};

SoundInterface_struct *SNDCoreList[] = {
  &SNDDummy,
#ifdef HAVE_LIBSDL
  &SNDSDL,
#endif
  NULL
};

VideoInterface_struct *VIDCoreList[] = {
  &VIDDummy,
  &VIDOGL,
  NULL
};


#ifdef YAB_PORT_OSD
#include "nanovg/nanovg_osdcore.h"
OSD_struct *OSDCoreList[] = {
  &OSDNnovg,
  &OSDDummy,
  NULL
};
#endif


static SDL_Window* wnd;
static SDL_GLContext glc;
int g_EnagleFPS = 1;
int g_resolution_mode = 0;
int g_keep_aspect_rate = 0;
int g_scsp_sync = 1;
int g_frame_skip = 0;
int g_emulated_bios = 0;
int g_video_filter = 0;
int g_polygon_generation = 0;
int g_rbg_resolution = 0;
int g_rbg_compute_shader = 0;
int g_extend_internal_memory = 0;
int g_rotate_screen = 0;
int g_scsp_sync_time_mode = 1;
int g_cpu_sync_shift = 0;
int g_frame_limit = 0;
int g_cartridge = 0;
int g_region = 0;
int g_video_format = 0;
int g_sound_engine = 1;
//InputManager* inputmng;
//MenuScreen * menu;

using std::string;
string g_keymap_filename;

#include "nanovg.h"

//----------------------------------------------------------------------------------------------
NVGcontext * getGlobalNanoVGContext(){
  return NULL;
}

void DrawDebugInfo()
{
}

void YuiErrorMsg(const char *string)
{
  YabLogWrite("error: %s", string ? string : "(null)");
}

void YuiSwapBuffers(void)
{
  static unsigned long long present_count = 0;
  ++present_count;
  const GLenum before = glGetError();
  eglSwapBuffers(s_display, s_surface);
  const GLenum after = glGetError();
  if (before != GL_NO_ERROR || after != GL_NO_ERROR)
    YabLogWrite("present gl_error_before=0x%04x after=0x%04x", before, after);
  if (present_count <= 3 || (present_count % 60) == 0)
    YabLogWrite("present frame=%llu", present_count);
  SetOSDToggle(g_EnagleFPS);
}

//void YuiSwapBuffers(void)
//{
//  SDL_GL_SwapWindow(wnd);
//  SetOSDToggle(g_EnagleFPS);
//}

int YuiRevokeOGLOnThisThread(){
  YabLogWrite("revoke GL thread");
  //SDL_GL_MakeCurrent(wnd,nullptr);
  eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  return 0;
}

int YuiUseOGLOnThisThread(){
  YabLogWrite("use GL thread");
  //SDL_GL_MakeCurrent(wnd,glc);
  eglMakeCurrent(s_display, s_surface, s_surface, s_context);
  return 0;
}

}

int saveScreenshot( const char * filename );

int padmode = 0;

int yabauseinit()
{
  int res;
  yabauseinit_struct yinit = {};

  yinit.m68kcoretype = M68KCORE_MUSASHI;
  yinit.percoretype = PERCORE_DUMMY;
#ifdef SH2_DYNAREC
  // Use the Switch dynarec core; the interpreter can be too slow during BIOS
  // startup and may leave TVMD disabled for an extended period.
  yinit.sh2coretype = SH2CORE_DYNAREC;
#else
  yinit.sh2coretype = SH2CORE_INTERPRETER;
#endif
  //yinit.vidcoretype = VIDCORE_SOFT;
  yinit.vidcoretype = VIDCORE_OGL;
  yinit.sndcoretype = SNDCORE_DUMMY;
  //yinit.sndcoretype = SNDCORE_DUMMY;
  //yinit.cdcoretype = CDCORE_DEFAULT;
  yinit.cdcoretype = CDCORE_ISO;
  yinit.carttype = g_cartridge;
  yinit.regionid = (u8)g_region;
  if( g_emulated_bios ){
    yinit.biospath = NULL;
  }else{
    yinit.biospath = biospath;
  }
  yinit.cdpath = cdpath;
  yinit.buppath = buppath;
  yinit.mpegpath = mpegpath;
  yinit.cartpath = cartpath;
  yinit.videoformattype = g_video_format;
  yinit.frameskip = g_frame_skip;
  yinit.framelimit = g_frame_limit;
  yinit.usethreads = 0;
  yinit.skip_load = 0;    
  yinit.video_filter_type = g_video_filter;
  yinit.polygon_generation_mode = g_polygon_generation;
  yinit.use_new_scsp = g_sound_engine;
  yinit.resolution_mode = g_resolution_mode;
  yinit.rbg_resolution_mode = g_rbg_resolution;
  yinit.rbg_use_compute_shader = g_rbg_compute_shader;
  yinit.rotate_screen = g_rotate_screen;
  yinit.scsp_sync_count_per_frame = g_scsp_sync;
  yinit.extend_backup = g_extend_internal_memory;
  yinit.scsp_main_mode = g_scsp_sync_time_mode;
  yinit.sync_shift = g_cpu_sync_shift;

  res = YabauseInit(&yinit);
  YabLogWrite("YabauseInit returned %d", res);
  if( res != 0) {
    YabLogWrite("YabauseInit failed (res=%d)", res);
    return -1;
  }

  //padmode = inputmng->getCurrentPadMode( 0 );
  OSDInit(0);
  OSDChangeCore(OSDCORE_NANOVG);
  LogStart();
  //LogChangeOutput(DEBUG_CALLBACK, NULL);
  return 0;
}

static bool initEgl(NWindow* win)
{
    if (!eglBindAPI(EGL_OPENGL_ES_API))
    {
        TRACE("EGL bind OpenGL ES API failed: %d", eglGetError());
        return false;
    }
    // Connect to the EGL default display
    s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!s_display)
    {
        TRACE("Could not connect to display! error: %d", eglGetError());
        goto _fail0;
    }

    // Initialize the EGL display connection
    if (!eglInitialize(s_display, NULL, NULL))
    { TRACE("EGL initialize failed: %d", eglGetError()); goto _fail0; }

    // Get an appropriate EGL framebuffer configuration
    EGLConfig config;
    EGLint numConfigs;
    static const EGLint framebufferAttributeList[] =
    {
        EGL_RED_SIZE,     8,
        EGL_GREEN_SIZE,   8,
        EGL_BLUE_SIZE,    8,
        EGL_ALPHA_SIZE,   8,
        EGL_DEPTH_SIZE,   24,
        EGL_STENCIL_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    if (!eglChooseConfig(s_display, framebufferAttributeList, &config, 1, &numConfigs))
    { TRACE("EGL choose config failed: %d", eglGetError()); goto _fail1; }
    if (numConfigs == 0)
    {
        TRACE("No config found! error: %d", eglGetError());
        goto _fail1;
    }

    // Create an EGL window surface
    s_surface = eglCreateWindowSurface(s_display, config, win, NULL);
    if (!s_surface)
    {
        TRACE("Surface creation failed! error: %d", eglGetError());
        goto _fail1;
    }

    // Create an EGL rendering context
    static const EGLint contextAttributeList[] =
    {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, contextAttributeList);
    if (!s_context)
    {
        TRACE("Context creation failed! error: %d", eglGetError());
        goto _fail2;
    }

    // Connect the context to the surface
    if (!eglMakeCurrent(s_display, s_surface, s_surface, s_context))
    { TRACE("EGL make current failed: %d", eglGetError()); goto _fail3; }
    eglSwapInterval(s_display, 1);
    return true;

_fail3:
    eglDestroyContext(s_display, s_context); s_context = NULL;
_fail2:
    eglDestroySurface(s_display, s_surface);
    s_surface = NULL;
_fail1:
    eglTerminate(s_display);
    s_display = NULL;
_fail0:
    return false;
}

static void deinitEgl()
{
    if (s_display)
    {
        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_context)
        {
            eglDestroyContext(s_display, s_context);
            s_context = NULL;
        }
        if (s_surface)
        {
            eglDestroySurface(s_display, s_surface);
            s_surface = NULL;
        }
        eglTerminate(s_display);
        s_display = NULL;
    }
}

//==============================================================================
// Built-in boot page (file selection UI)
//==============================================================================
namespace
{
NVGcontext* g_ui_vg = nullptr;
GbBoot::UiFontSet g_ui_fonts;

uint64_t TickNowMs()
{
  static const uint64_t freq = armGetSystemTickFreq();
  return armGetSystemTick() * 1000ULL / freq;
}

void ResetGlStateForUi()
{
  glUseProgram(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

bool EnsureUiReady(const std::string& font_dir)
{
  if (g_ui_vg)
    return true;
  g_ui_vg = nvgCreateGLES3(NVG_ANTIALIAS);
  if (!g_ui_vg)
  {
    YabLogWrite("bootpage: nvgCreateGLES3 failed");
    return false;
  }
  if (!GbBoot::UiFontsLoad(g_ui_vg, g_ui_fonts,
                           font_dir.empty() ? nullptr : font_dir.c_str()))
  {
    YabLogWrite("bootpage: UiFontsLoad failed");
    return false;
  }
  YabLogWrite("bootpage fonts text=%d bold=%d switch=%d material=%d", g_ui_fonts.text,
              g_ui_fonts.text_bold, g_ui_fonts.icons_switch, g_ui_fonts.icons_material);
  return true;
}

void DestroyUi()
{
  if (g_ui_vg)
  {
    nvgDeleteGLES3(g_ui_vg);
    g_ui_vg = nullptr;
  }
}

std::string NameOf(const std::string& path)
{
  const auto slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

void ShowLaunchOverlay(const std::string& rom, int fb_w, int fb_h)
{
  if (!g_ui_vg)
    return;
  glViewport(0, 0, fb_w, fb_h);
  GbBoot::NvgUiDrawLaunchOverlay(g_ui_vg, g_ui_fonts, fb_w, fb_h, NameOf(rom).c_str());
  eglSwapBuffers(s_display, s_surface);
}

struct RepeatKey
{
  bool held = false;
  uint64_t next_ms = 0;
};

void UpdateRepeat(bool held_now, uint64_t now_ms, RepeatKey& state, int& action)
{
  action = 0;
  if (!held_now)
  {
    state.held = false;
    return;
  }
  if (!state.held)
  {
    state.held = true;
    state.next_ms = now_ms + 280;
    action = 1;
  }
  else if (now_ms >= state.next_ms)
  {
    state.next_ms = now_ms + 130;
    action = 2;
  }
}

// Shows the boot page and blocks until a game is picked (returns true, out_rom
// filled) or the user quits (returns false).  Cleans up the UI GL context.
bool RunBootPage(std::string& out_rom, const std::string& font_dir,
                 const std::string& rom_root,
                 const std::map<std::string, std::string>& config,
                 const std::string& config_path)
{
  int fb_w = 1280;
  int fb_h = 720;
  {
    u32 w = 1280;
    u32 h = 720;
    nwindowGetDimensions(nwindowGetDefault(), &w, &h);
    fb_w = static_cast<int>(w);
    fb_h = static_cast<int>(h);
  }

  PadState pad;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);

  GbBoot::DirBrowser browser;
  std::string status;
  if (!EnsureUiReady(font_dir))
  {
    DestroyUi();
    return false;
  }

  std::string err;
  if (!browser.Open(rom_root, err))
  {
    status = "Cannot open " + rom_root;
    YabLogWrite("bootpage: cannot open %s", rom_root.c_str());
  }
  YabLogWrite("bootpage root=%s entries=%u", rom_root.c_str(),
              static_cast<unsigned>(browser.Entries().size()));

  bool settings_mode = false;
  std::vector<GbBoot::SettingItem> settings;
  int settings_cursor = 0;
  int settings_scroll = 0;
  std::string settings_status;

  bool selected = false;
  while (appletMainLoop())
  {
    ResetGlStateForUi();
    glViewport(0, 0, fb_w, fb_h);

    padUpdate(&pad);
    const u64 kHeld = padGetButtons(&pad);
    const u64 kDown = padGetButtonsDown(&pad);

    GbBoot::UiInputState in;
    in.up = (kHeld & HidNpadButton_Up) != 0;
    in.down = (kHeld & HidNpadButton_Down) != 0;
    in.left = (kHeld & HidNpadButton_Left) != 0;
    in.right = (kHeld & HidNpadButton_Right) != 0;
    in.a = (kDown & HidNpadButton_A) != 0;
    in.b = (kDown & HidNpadButton_B) != 0;
    in.x = (kDown & HidNpadButton_X) != 0;
    in.y = (kDown & HidNpadButton_Y) != 0;
    in.minus = (kDown & HidNpadButton_Minus) != 0;
    in.plus = (kDown & HidNpadButton_Plus) != 0;
    in.upPressed = (kDown & HidNpadButton_Up) != 0;
    in.downPressed = (kDown & HidNpadButton_Down) != 0;
    in.leftPressed = (kDown & HidNpadButton_Left) != 0;
    in.rightPressed = (kDown & HidNpadButton_Right) != 0;

    // Exit: Plus (or L stick press on Switch).
    if (in.plus || (kDown & HidNpadButton_StickL))
      break;

    const uint64_t now_ms = TickNowMs();
    static RepeatKey repeat_up;
    static RepeatKey repeat_down;
    int action_up = 0;
    int action_down = 0;
    UpdateRepeat(in.up, now_ms, repeat_up, action_up);
    UpdateRepeat(in.down, now_ms, repeat_down, action_down);

    if (!settings_mode)
    {
      if (action_up)
        browser.MoveCursor(-1);
      if (action_down)
        browser.MoveCursor(1);
      if (in.x)
        browser.CycleSort();
      if (in.y)
        browser.SetGamesOnly(!browser.GamesOnly());
      if (in.minus)
      {
        if (settings.empty())
          settings = GbBoot::BuildCoreSettings(config);
        settings_mode = true;
        settings_status.clear();
      }
      if (in.a && browser.Activate())
      {
        out_rom = browser.Selected();
        selected = true;
        break;
      }
      if (in.b)
        browser.GoParent();
    }
    else
    {
      const int count = static_cast<int>(settings.size());
      if (count > 0)
      {
        if (action_up)
          settings_cursor = (settings_cursor - 1 + count) % count;
        if (action_down)
          settings_cursor = (settings_cursor + 1) % count;

        if (in.leftPressed || in.rightPressed || in.a)
        {
          const int direction = in.leftPressed ? -1 : 1;
          GbBoot::ChangeSetting(settings[settings_cursor], direction);
          if (GbBoot::SaveSettings(config_path, settings))
            settings_status = "已保存,重启后生效";
          else
            settings_status = "保存失败";
        }
      }
      if (in.b || in.minus)
        settings_mode = false;

      // Keep the cursor visible.
      const float scale = fb_h / 720.0f;
      const int rows = static_cast<int>((fb_h - 98.0f * scale) / (46.0f * scale));
      if (rows > 0)
      {
        if (settings_cursor < settings_scroll)
          settings_scroll = settings_cursor;
        else if (settings_cursor >= settings_scroll + rows)
          settings_scroll = settings_cursor - rows + 1;
      }
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (settings_mode)
      GbBoot::NvgUiDrawSettings(g_ui_vg, settings, g_ui_fonts, fb_w, fb_h,
                                settings_cursor, settings_scroll,
                                settings_status.empty() ? nullptr : settings_status.c_str());
    else
      GbBoot::NvgUiDrawBrowser(g_ui_vg, browser, g_ui_fonts, fb_w, fb_h,
                               status.empty() ? nullptr : status.c_str());
    eglSwapBuffers(s_display, s_surface);
    svcSleepThread(16ULL * 1000000ULL);
  }

  if (selected)
    YabLogWrite("bootpage selected rom=%s", out_rom.c_str());
  else
    YabLogWrite("bootpage exited without selection");

  DestroyUi();
  ResetGlStateForUi();
  return selected;
}

void ReturnToLauncher(const std::string& return_nro, const std::string& session)
{
  if (return_nro.empty() || !envHasNextLoad())
    return;
  const std::string args = return_nro +
      (session.empty() ? " --resume" : " --external-return " + session);
  envSetNextLoad(return_nro.c_str(), args.c_str());
}
}  // namespace


int main(int argc, char** argv)
{
  YabLogInit("sdmc:/GBAStation/log/ss.log");
  YabLogWrite("startup argc=%d", argc);
  for (int i = 0; i < argc; ++i) YabLogWrite("argv[%d]=%s", i, argv[i] ? argv[i] : "(null)");
  //inputmng = InputManager::getInstance();

  std::string bckup_dir = "./backup.bin";
  strcpy( buppath, bckup_dir.c_str() );

  const std::map<std::string, std::string> config = LoadGbaConfig();
  const char* configuredBios = ConfigValue(config, "core.saturn.biosPath", ConfigValue(config, "core.saturn.bios_path", ConfigValue(config, "saturn.biosPath", biospath)));
  const char* configuredBackup = ConfigValue(config, "core.saturn.savePath", ConfigValue(config, "core.saturn.backupPath", ConfigValue(config, "saturn.savePath", buppath)));
  snprintf(biospath, sizeof biospath, "%s", NormalizeSwitchPath(configuredBios).c_str());
  snprintf(buppath, sizeof buppath, "%s", NormalizeSwitchPath(configuredBackup).c_str());
  g_resolution_mode = ConfigInt(config, "core.saturn.resolution_mode",
                       ConfigInt(config, "core.saturn.resolutionMode", 0));
  g_keep_aspect_rate = ConfigInt(config, "core.saturn.aspect_ratio",
                        ConfigInt(config, "core.saturn.keep_aspect_rate",
                         ConfigInt(config, "core.saturn.keepAspectRatio", 0)));
  g_scsp_sync = ConfigInt(config, "core.saturn.scsp_sync_per_frame",
               ConfigInt(config, "core.saturn.scspSyncPerFrame", 1));
  g_frame_skip = ConfigInt(config, "core.saturn.frame_skip",
                 ConfigInt(config, "core.saturn.frameSkip", 0));
  g_frame_limit = ConfigInt(config, "core.saturn.frame_limit", 0);
  g_video_filter = ConfigInt(config, "core.saturn.video_filter", 0);
  g_polygon_generation = ConfigInt(config, "core.saturn.polygon_generation", 0);
  g_rbg_resolution = ConfigInt(config, "core.saturn.rbg_resolution", 0);
  g_rbg_compute_shader = ConfigBool(config, "core.saturn.rbg_compute_shader", false) ? 1 : 0;
  g_extend_internal_memory = ConfigBool(config, "core.saturn.extend_internal_memory", false) ? 1 : 0;
  g_rotate_screen = ConfigBool(config, "core.saturn.rotate_screen", false) ? 1 : 0;
  g_scsp_sync_time_mode = ConfigInt(config, "core.saturn.scsp_sync_time_mode", 1);
  g_sound_engine = ConfigInt(config, "core.saturn.sound_engine", 1);
  g_cartridge = ConfigInt(config, "core.saturn.cartridge", 0);
  g_region = ConfigInt(config, "core.saturn.region", 0);
  g_video_format = ConfigInt(config, "core.saturn.video_format", 0);
  g_emulated_bios = ConfigBool(config, "core.saturn.emulated_bios",
                     ConfigBool(config, "core.saturn.emulatedBios", false)) ? 1 : 0;
  // cpu_sync_per_line is expressed as calls per line (1/2/4/8); the core uses
  // the corresponding power-of-two shift.
  switch (ConfigInt(config, "core.saturn.cpu_sync_per_line", 1)) {
    case 2:  g_cpu_sync_shift = 1; break;
    case 4:  g_cpu_sync_shift = 2; break;
    case 8:  g_cpu_sync_shift = 3; break;
    default: g_cpu_sync_shift = 0; break;
  }

  // GBAStation Switch protocol: positional ROM path, optional return NRO and session token.
  std::string return_nro;
  std::string session_token;
  for (int i = 1; i < argc; ++i) {
    if (!argv[i]) continue;
    if (!strcmp(argv[i], "--return") && i + 1 < argc) { return_nro = argv[++i]; YabLogWrite("return NRO=%s", return_nro.c_str()); continue; }
    if (!strcmp(argv[i], "--gbastation-session") && i + 1 < argc) { session_token = argv[++i]; YabLogWrite("session=%s", session_token.c_str()); continue; }
    if (argv[i][0] != '-' && cdpath[0] == '\0') {
      const std::string normalized = NormalizeSwitchPath(argv[i]);
      snprintf(cdpath, sizeof cdpath, "%s", normalized.c_str());
    }
  }
  YabLogWrite("launch rom=%s bios=%s backup=%s", cdpath, biospath, buppath);
  YabLogWrite("settings emulatebios=%d resolution=%d frameskip=%d framelimit=%d filter=%d polygon=%d aspect=%d rotate=%d rbg_res=%d rbg_cs=%d extmem=%d sound=%d scsp_sync=%d scsp_mode=%d cpu_sync_shift=%d cart=%d region=%d format=%d",
              g_emulated_bios, g_resolution_mode, g_frame_skip, g_frame_limit, g_video_filter,
              g_polygon_generation, g_keep_aspect_rate, g_rotate_screen, g_rbg_resolution,
              g_rbg_compute_shader, g_extend_internal_memory, g_sound_engine, g_scsp_sync,
              g_scsp_sync_time_mode, g_cpu_sync_shift, g_cartridge, g_region, g_video_format);
  {
    FILE *bf = fopen(biospath, "rb");
    FILE *rf = fopen(cdpath, "rb");
    YabLogWrite("input files bios=%s rom=%s", bf ? "ok" : "missing", rf ? "ok" : "missing");
    if (bf) { fseek(bf, 0, SEEK_END); YabLogWrite("bios size=%ld", ftell(bf)); fclose(bf); }
    if (rf) { fseek(rf, 0, SEEK_END); YabLogWrite("rom size=%ld", ftell(rf)); fclose(rf); }
  }

#if 0  
  printf("\033[2J");

  // Inisialize home directory
  std::string home_dir = getenv("HOME");
  home_dir += "/.yabasanshiro/";
  struct stat st = {0};
  if (stat(home_dir.c_str(), &st) == -1) {
    mkdir(home_dir.c_str(), 0700);
  }  
  std::string bckup_dir = home_dir + "backup.bin";
  strcpy( buppath, bckup_dir.c_str() );

  g_keymap_filename = home_dir + "keymapv2.json";

  std::string current_exec_name = argv[0]; // Name of the current exec program
  std::vector<std::string> all_args;
  if (argc > 1) {
    all_args.assign(argv + 1, argv + argc);
    if( all_args[0] == "-h" || all_args[0] == "--h" ){
      printf("Usage:\n");
      printf("  -b STRING  --bios STRING                 bios file\n");
      printf("  -i STRING  --iso STRING                  iso/cue file\n");
      printf("  -r NUMBER  --resolution_mode NUMBER      0 .. Native, 1 .. 4x, 2 .. 2x, 3 .. Original\n");
      printf("  -a         --keep_aspect_rate\n");
      printf("  -s NUMBER  --scps_sync_per_frame NUMBER\n");
      printf("  -nf         --no_frame_skip              disable frame skip\n");    
      printf("  -v         --version\n");    
      exit(0);
    }
  }

  for( int i=0; i<all_args.size(); i++ ){
    string x = all_args[i];
		if(( x == "-b" || x == "--bios") && (i+1<all_args.size() ) ) {
      g_emulated_bios = 0;
      strncpy(biospath, all_args[i+1].c_str(), 256);
    }
		else if(( x == "-i" || x == "--iso") && (i+1<all_args.size() ) ) {
      strncpy(cdpath, all_args[i+1].c_str(), 256);
    }
		else if(( x == "-r" || x == "--resolution_mode") && (i+1<all_args.size() ) ) {
      g_resolution_mode = std::stoi( all_args[i+1] );
    }
		else if(( x == "-a" || x == "--keep_aspect_rate") ) {
      g_keep_aspect_rate = 1;
    }
		else if(( x == "-s" || x == "--g_scsp_sync")&& (i+1<all_args.size() ) ) {
      g_scsp_sync = std::stoi( all_args[i+1] );
    }
		else if(( x == "-nf" || x == "--no_frame_skip") ) {
      g_frame_skip = 0;
    }
		else if(( x == "-v" || x == "--version") ) {
      printf("YabaSanshiro version %s(%s)\n",YAB_VERSION, GIT_SHA1 );
      return 0;
    }
	}
#endif
  if (!initEgl(nwindowGetDefault()))
  { YabLogWrite("EGL initialization failed"); YabLogShutdown();
      return EXIT_FAILURE;
  }

  printf("context renderer string: \"%s\"\n", glGetString(GL_RENDERER));
  printf("context vendor string: \"%s\"\n", glGetString(GL_VENDOR));
  printf("version string: \"%s\"\n", glGetString(GL_VERSION));
  printf("Extentions: %s\n",glGetString(GL_EXTENSIONS));

  YabLogWrite("EGL renderer=%s version=%s", glGetString(GL_RENDERER), glGetString(GL_VERSION));

  // No ROM argument: show the built-in file selection boot page.
  if (cdpath[0] == '\0') {
    const Result romfs_rc = romfsInit();
    YabLogWrite("romfsInit result=0x%x", static_cast<unsigned>(romfs_rc));
    const std::string romRoot = ConfigValue(config, "core.saturn.rom_dir",
        ConfigValue(config, "core.saturn.romPath", "sdmc:/GBAStation/roms/saturn"));
    const std::string fontDir = ConfigValue(config, "core.saturn.font_dir",
        ConfigValue(config, "core.saturn.fontDir", "sdmc:/GBAStation/font"));
    std::string picked;
    if (!RunBootPage(picked, fontDir, romRoot, config,
                     "sdmc:/GBAStation/config/config.cfg")) {
      deinitEgl();
      ReturnToLauncher(return_nro, session_token);
      YabLogWrite("bootpage quit, exit");
      YabLogShutdown();
      return 0;
    }
    snprintf(cdpath, sizeof cdpath, "%s", picked.c_str());
    YabLogWrite("bootpage launching rom=%s", cdpath);
  }

  YabLogWrite("yabauseinit begin");
  if( yabauseinit() == -1 ) {
      printf("Fail to yabauseinit Bye! (%s)", SDL_GetError() );
      YabLogWrite("yabauseinit failed"); YabLogShutdown();
      return -1;
  }
  YabLogWrite("yabauseinit complete");
  YabLogWrite("saturn state emulatebios=%d ssh2running=%d", yabsys.emulatebios, yabsys.IsSSH2Running);
  GLint initFbo = -1;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &initFbo);
  YabLogWrite("render init framebuffer=%d viewport=%d,%d", initFbo, 1280, 720);

  int width = 1280;
  int height = 720;
  VIDCore->Resize(0,0,width,height,0,g_keep_aspect_rate);
  glViewport(0,0,width,height);
  glClearColor( 0.0f, 0.0f,0.0f,1.0f);
  glClear( GL_COLOR_BUFFER_BIT );
  glFlush();
  eglSwapBuffers(s_display, s_surface);
  // Verify that the Switch EGL surface itself is visible before the emulator
  // starts drawing.  Keep the clear black so this is invisible to users.
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, width, height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glFlush();
  eglSwapBuffers(s_display, s_surface);
  YabLogWrite("initial surface present gl_error=0x%04x", glGetError());

#if 0
  inputmng->init(g_keymap_filename);
  menu = new MenuScreen(wnd,width,height, g_keymap_filename);
  menu->setConfigFile(g_keymap_filename);  


  if( g_keep_aspect_rate ){
    int originx = 0;
    int originy = 0;
    int specw = width;
    int spech = height;
    float specratio = (float)specw / (float)spech;
    int saturnw = 4;
    int saturnh = 3;
    float saturnraito = (float)saturnw/ (float)saturnh;
    float revraito = (float) saturnh/ (float)saturnw;
    if( specratio > saturnraito ){
            width = spech * saturnraito;
            height = spech;
            originx = (dsp.w - width)/2.0;
            originy = 0;
    }else{
        width = specw ;
        height = specw * revraito;
        originx = 0;
        originy = spech - height;
    }
    VIDCore->Resize(originx,originy,width,height,0);
  }else{
    VIDCore->Resize(0,0,width,height,0);
  }
  SDL_GL_MakeCurrent(wnd,nullptr);
  YabThreadSetCurrentThreadAffinityMask(0x00);

  Uint32 evToggleMenu = SDL_RegisterEvents(1);
  inputmng->setToggleMenuEventCode(evToggleMenu);

  Uint32  evResetMenu = SDL_RegisterEvents(1);
  menu->setResetMenuEventCode(evResetMenu);

  Uint32  evPadMenu = SDL_RegisterEvents(1);
  menu->setTogglePadModeMenuEventCode(evPadMenu);

  Uint32  evToggleFps = SDL_RegisterEvents(1);
  menu->setToggleFpsCode(evToggleFps);

  Uint32  evToggleFrameSkip = SDL_RegisterEvents(1);
  menu->setToggleFrameSkip(evToggleFrameSkip);

  Uint32  evUpdateConfig = SDL_RegisterEvents(1);
  menu->setUpdateConfig(evUpdateConfig);

  Uint32  evOpenTray = SDL_RegisterEvents(1);
  menu->setOpenTrayMenuEventCode(evOpenTray);

  Uint32  evCloseTray = SDL_RegisterEvents(1);
  menu->setCloseTrayMenuEventCode(evCloseTray);


  bool menu_show = false;
  std::string tmpfilename = home_dir + "tmp.png";

  struct sched_param thread_param;
  thread_param.sched_priority = 15; //sched_get_priority_max(SCHED_FIFO);
  if ( pthread_setschedparam(pthread_self(), SCHED_FIFO, &thread_param) < -1 ) {
    LOG("sched_setscheduler");
  }
  setpriority( PRIO_PROCESS, 0, -8);
  int frame_cont = 0;
  while(true) {
    SDL_Event e;
    while(SDL_PollEvent(&e)) {
      if(e.type == SDL_QUIT){
        glClearColor(0.0,0.0,0.0,1.0);
        glClear(GL_COLOR_BUFFER_BIT);        
        SDL_GL_SwapWindow(wnd);
        YabauseDeInit();
        SDL_Quit();
        return 0;
      }
      else if( e.type == evUpdateConfig ){
          inputmng->updateConfig();
      }
      else if(e.type == evToggleMenu){
        if( menu_show ){
          menu_show = false;
          inputmng->setMenuLayer(nullptr);
          SDL_GL_MakeCurrent(wnd,nullptr);
          VdpResume();
          SNDSDL.UnMuteAudio();          
        }else{
          menu_show = true;
          SNDSDL.MuteAudio();
          VdpRevoke();
          inputmng->setMenuLayer(menu);
          SDL_GL_MakeCurrent(wnd,glc);
          saveScreenshot(tmpfilename.c_str());
          glUseProgram(0);
          glGetError();
          glBindBuffer(GL_ARRAY_BUFFER, 0);
          glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
          glDisableVertexAttribArray(0);
          glDisableVertexAttribArray(1);
          glDisableVertexAttribArray(2);
          glDisable(GL_DEPTH_TEST);
          glDisable(GL_SCISSOR_TEST);
          glDisable(GL_STENCIL_TEST);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   
          menu->setBackGroundImage( tmpfilename );
        }
      }

      else if(e.type == evResetMenu){
        YabauseReset();
        menu_show = false;
        inputmng->setMenuLayer(nullptr);
        SDL_GL_MakeCurrent(wnd,nullptr);
        VdpResume();
        SNDSDL.UnMuteAudio(); 
      }

      else if(e.type == evPadMenu ){
        if( padmode == 0 ){
          padmode = 1;
        }else{
          padmode = 0;
        }
        inputmng->setGamePadomode( 0, padmode );
        menu_show = false;
        inputmng->setMenuLayer(nullptr);
        SDL_GL_MakeCurrent(wnd,nullptr);
        VdpResume();
        SNDSDL.UnMuteAudio();         
      }

      else if(e.type == evToggleFps ){
        if( g_EnagleFPS == 0 ){
          g_EnagleFPS = 1;
        }else{
          g_EnagleFPS = 0;
        }
        menu_show = false;
        inputmng->setMenuLayer(nullptr);
        SDL_GL_MakeCurrent(wnd,nullptr);
        VdpResume();
        SNDSDL.UnMuteAudio();         
      }

      else if(e.type == evToggleFrameSkip ){
        if( g_frame_skip == 0 ){
          g_frame_skip = 1;
          EnableAutoFrameSkip();
        }else{
          g_frame_skip = 0;
          DisableAutoFrameSkip();
        }
        menu_show = false;
        inputmng->setMenuLayer(nullptr);
        SDL_GL_MakeCurrent(wnd,nullptr);
        VdpResume();
        SNDSDL.UnMuteAudio();         
      }
      else if(e.type == evOpenTray ){
        menu->setCurrentGamePath(cdpath);
        Cs2ForceOpenTray();
        if( !g_emulated_bios ) {
        menu_show = false;
        inputmng->setMenuLayer(nullptr);
        SDL_GL_MakeCurrent(wnd,nullptr);
        VdpResume();
        SNDSDL.UnMuteAudio();         
      }
      }
      else if(e.type == evCloseTray ){
        if( e.user.data1 != nullptr ){
          strcpy( cdpath, (const char*)e.user.data1 );
          free(e.user.data1);
        }
        Cs2ForceCloseTray(CDCORE_ISO, cdpath );
        menu_show = false;
        inputmng->setMenuLayer(nullptr);
        SDL_GL_MakeCurrent(wnd,nullptr);
        VdpResume();
        SNDSDL.UnMuteAudio();         
      }

      inputmng->parseEvent(e);
      if( menu_show ){
        menu->onEvent( e );
      }
    }
    inputmng->handleJoyEvents();

    if( menu_show ){
      glClearColor(0.0f, 0.0f, 0.0f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      menu->drawAll();
      SDL_GL_SwapWindow(wnd);
    }else{
      //printf("\033[%d;%dH Frmae = %d \n", 0, 0, frame_cont);
      //frame_cont++;
      YabauseExec(); // exec one frame
    }
  }
#else

#define MAKE_PAD(a,b) ((a<<24)|(b))
  void * padbits;
  PerPortReset();
  padbits = PerPadAdd(&PORTDATA1);

  PerSetKey(MAKE_PAD(0,PERPAD_UP), PERPAD_UP, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_RIGHT), PERPAD_RIGHT, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_DOWN), PERPAD_DOWN, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_LEFT), PERPAD_LEFT, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_START), PERPAD_START, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_A), PERPAD_A, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_B), PERPAD_B, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_C), PERPAD_C, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_X), PERPAD_X, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_Y), PERPAD_Y, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_Z), PERPAD_Z, padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_RIGHT_TRIGGER),PERPAD_RIGHT_TRIGGER,padbits);
  PerSetKey(MAKE_PAD(0,PERPAD_LEFT_TRIGGER),PERPAD_LEFT_TRIGGER,padbits);

  unsigned long long frame_count = 0;
  YabLogWrite("emulation loop begin");
  while(appletMainLoop()) {
#if 0
      // Get and process input
        hidScanInput();
        u32 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
        if ( (kDown & KEY_MINUS) ) 
            break;

    int player = 0;
    if (kDown & KEY_B) PerKeyDown(MAKE_PAD(player,PERPAD_A)); else PerKeyUp(MAKE_PAD(player,PERPAD_A));
    if (kDown & KEY_A) PerKeyDown(MAKE_PAD(player,PERPAD_B)); else PerKeyUp(MAKE_PAD(player,PERPAD_B));
    if (kDown & KEY_R) PerKeyDown(MAKE_PAD(player,PERPAD_C)); else PerKeyUp(MAKE_PAD(player,PERPAD_C));

    if (kDown & KEY_Y) PerKeyDown(MAKE_PAD(player,PERPAD_X)); else PerKeyUp(MAKE_PAD(player,PERPAD_X));
    if (kDown & KEY_X) PerKeyDown(MAKE_PAD(player,PERPAD_Y)); else PerKeyUp(MAKE_PAD(player,PERPAD_Y));
    if (kDown & KEY_L) PerKeyDown(MAKE_PAD(player,PERPAD_Z)); else PerKeyUp(MAKE_PAD(player,PERPAD_Z));

    if (kDown & KEY_DLEFT) PerKeyDown(MAKE_PAD(player,PERPAD_LEFT)); else PerKeyUp(MAKE_PAD(player,PERPAD_LEFT));
    if (kDown & KEY_DRIGHT) PerKeyDown(MAKE_PAD(player,PERPAD_RIGHT)); else PerKeyUp(MAKE_PAD(player,PERPAD_RIGHT));
    if (kDown & KEY_DUP) PerKeyDown(MAKE_PAD(player,PERPAD_UP)); else PerKeyUp(MAKE_PAD(player,PERPAD_UP));
    if (kDown & KEY_DDOWN) PerKeyDown(MAKE_PAD(player,PERPAD_DOWN)); else PerKeyUp(MAKE_PAD(player,PERPAD_DOWN));

    if (kDown & KEY_PLUS) PerKeyDown(MAKE_PAD(player,PERPAD_START)); else PerKeyUp(MAKE_PAD(player,PERPAD_START));

    if (kDown & KEY_ZR) PerKeyDown(MAKE_PAD(player,PERPAD_RIGHT_TRIGGER)); else PerKeyUp(MAKE_PAD(player,PERPAD_RIGHT_TRIGGER));
    if (kDown & KEY_ZL) PerKeyDown(MAKE_PAD(player,PERPAD_LEFT_TRIGGER)); else PerKeyUp(MAKE_PAD(player,PERPAD_LEFT_TRIGGER));

    //glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    //glClear(GL_COLOR_BUFFER_BIT);
    //eglSwapBuffers(s_display, s_surface);
    YabauseExec(); // exec one frame
#endif
    const bool trace_boot = frame_count >= 120 && frame_count < 240;
    if (trace_boot) YabLogWrite("boot frame=%llu exec begin", frame_count + 1);
    YabauseExec();
    if (trace_boot) YabLogWrite("boot frame=%llu exec end", frame_count + 1);
    glFlush();
    ++frame_count;
    if ((frame_count % 60) == 0) {
      YabLogWrite("emulation frame=%llu swap_error=0x%04x", frame_count, eglGetError());
      if (MSH2) YabLogWrite("master sh2 pc=0x%08x cycles=%u sleep=%d", MSH2->regs.PC, MSH2->cycles, MSH2->isSleeping);
      if (SSH2) YabLogWrite("slave sh2 pc=0x%08x cycles=%u sleep=%d", SSH2->regs.PC, SSH2->cycles, SSH2->isSleeping);
    }
  }
#endif  
  YabauseDeInit();
  deinitEgl();
  ReturnToLauncher(return_nro, session_token);
  YabLogWrite("normal exit");
  YabLogShutdown();
  return 0;
}


#define YUI_LOG printf
int saveScreenshot( const char * filename ){
    
#if 0    
    int width;
    int height;
    unsigned char * buf = NULL;
    unsigned char * bufRGB = NULL;
    png_bytep * row_pointers = NULL;
    int quality = 100; // best
    FILE * outfile = NULL;
    int row_stride;
    int glerror;
    int u,v;
    int pmode;
    png_byte color_type;
    png_byte bit_depth; 
    png_structp png_ptr;
    png_infop info_ptr;
    int number_of_passes;
    int rtn = -1;
  
    SDL_GetWindowSize( wnd, &width, &height);
    buf = (unsigned char *)malloc(width*height*4);
    if( buf == NULL ) {
        YUI_LOG("not enough memory\n");
        goto FINISH;
    }

    glReadBuffer(GL_BACK);
    pmode = GL_RGBA;
    glGetError();
    glReadPixels(0, 0, width, height, pmode, GL_UNSIGNED_BYTE, buf);
    if( (glerror = glGetError()) != GL_NO_ERROR ){
        YUI_LOG("glReadPixels %04X\n",glerror);
         goto FINISH;
    }
	
	for( u = 3; u <width*height*4; u+=4 ){
		buf[u]=0xFF;
	}
    row_pointers = (png_byte**)malloc(sizeof(png_bytep) * height);
    for (v=0; v<height; v++)
        row_pointers[v] = (png_byte*)&buf[ (height-1-v) * width * 4];

    // save as png
    if ((outfile = fopen(filename, "wb")) == NULL) {
        YUI_LOG("can't open %s\n", filename);
        goto FINISH;
    }

    /* initialize stuff */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr){
        YUI_LOG("[write_png_file] png_create_write_struct failed");
        goto FINISH;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr){
        YUI_LOG("[write_png_file] png_create_info_struct failed");
        goto FINISH;
    }

    if (setjmp(png_jmpbuf(png_ptr))){
        YUI_LOG("[write_png_file] Error during init_io");
        goto FINISH;
    }
    /* write header */
    png_init_io(png_ptr, outfile);
    
    if (setjmp(png_jmpbuf(png_ptr))){
        YUI_LOG("[write_png_file] Error during writing header");
        goto FINISH;
    }
    bit_depth = 8;
    color_type = PNG_COLOR_TYPE_RGB_ALPHA;
    png_set_IHDR(png_ptr, info_ptr, width, height,
        bit_depth, color_type, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    //png_set_gAMA(png_ptr, info_ptr, 1.0);
    {
        png_text text[3];
        int txt_fields = 0;
        char desc[256];
        
        time_t      gmt;
        png_time    mod_time;
        
        time(&gmt);
        png_convert_from_time_t(&mod_time, gmt);
        png_set_tIME(png_ptr, info_ptr, &mod_time);
    
        text[txt_fields].key = "Title";
        text[txt_fields].text = Cs2GetCurrentGmaecode();
        text[txt_fields].compression = PNG_TEXT_COMPRESSION_NONE;
        txt_fields++;

        sprintf( desc, "Yaba Sanshiro Version %s\n VENDER: %s\n RENDERER: %s\n VERSION %s\n",YAB_VERSION,glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
        text[txt_fields].key ="Description";
        text[txt_fields].text=desc;
        text[txt_fields].compression = PNG_TEXT_COMPRESSION_NONE;
        txt_fields++;
        
        png_set_text(png_ptr, info_ptr, text,txt_fields);
    }       
    png_write_info(png_ptr, info_ptr);


    /* write bytes */
    if (setjmp(png_jmpbuf(png_ptr))){
        YUI_LOG("[write_png_file] Error during writing bytes");
        goto FINISH;
    }
    png_write_image(png_ptr, row_pointers);

    /* end write */
    if (setjmp(png_jmpbuf(png_ptr))){
        YUI_LOG("[write_png_file] Error during end of write");
        goto FINISH;
    }
    
    png_write_end(png_ptr, NULL);
    rtn = 0;
FINISH: 
    if(outfile) fclose(outfile);
    if(buf) free(buf);
    if(bufRGB) free(bufRGB);
    if(row_pointers) free(row_pointers);
    return rtn;
#endif
  return 0;    
}
