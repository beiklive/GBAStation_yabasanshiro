// GBAStation core -- native desktop (macOS) frontend.
//
// SDL2 window + OpenGL 3.3 core profile.  The Saturn core is built without an
// OpenGL stack and uses its software renderer (VIDSoft).  Every finished frame
// is uploaded to a texture and drawn as a fullscreen quad; the built-in boot
// page (directory browser) is drawn with NanoVG on the same GL context.
//
// Keys (browser):
//   Up/Down            move        Right/Enter  open / launch
//   Left/Backspace     parent dir  Escape       quit
// Keys (in game):
//   D-pad = arrows, A/B/C = J/K/L, X/Y/Z = U/I/O, L/R = 1/2, Start = Enter
//   F5 quick save, F7 quick load, Escape = back to boot page / exit

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <OpenGL/gl3.h>

#include "../gbastation/GBAStationPlatform.h"
#include "../gbastation/Browser.h"
#include "../gbastation/NvgUi.h"
#include "../gbastation/UiFont.h"

extern "C" {
#include "config.h"
#include "yabause.h"
#include "vdp1.h"
#include "vdp2.h"
#include "vidsoft.h"
#include "scsp.h"
#include "peripheral.h"
#include "m68kcore.h"
#include "m68kmusashi.h"
#include "sh2core.h"
#include "sh2int.h"
#include "cdbase.h"
#include "cs2.h"
#include "memory.h"
#include "osdcore.h"
}

// Software renderer output (vidsoft.c).
extern "C" {
extern pixel_t* dispbuffer;
extern int vdp2width;
extern int vdp2height;
}

NVGcontext* UiCreateGL3Context();
extern "C" void nvgDeleteGL3(NVGcontext* ctx);

static SDL_Window* g_window = nullptr;
static SDL_GLContext g_glcontext = nullptr;
static int g_win_w = 1280;
static int g_win_h = 720;
static bool g_quit = false;

//==============================================================================
// Core lists (pluggable core selection the Yabause library needs)
//==============================================================================
M68K_struct* M68KCoreList[] = {
    &M68KDummy,
#ifdef HAVE_MUSASHI
    &M68KMusashi,
#endif
    NULL};

SH2Interface_struct* SH2CoreList[] = {
    &SH2Interpreter,
    &SH2DebugInterpreter,
    NULL};

PerInterface_struct* PERCoreList[] = {
    &PERDummy,
    NULL};

CDInterface* CDCoreList[] = {
    &DummyCD,
    &ISOCD,
    NULL};

SoundInterface_struct* SNDCoreList[] = {
    &SNDDummy,
    NULL};

VideoInterface_struct* VIDCoreList[] = {
    &VIDDummy,
    &VIDSoft,
    NULL};

//==============================================================================
// Software frame presenter (texture quad)
//==============================================================================
namespace
{
GLuint g_frame_program = 0;
GLuint g_frame_vao = 0;
GLuint g_frame_vbo = 0;
GLuint g_frame_texture = 0;
GLint g_frame_tex_loc = -1;
int g_frame_w = 0;
int g_frame_h = 0;

const char* kFrameVs = R"GLSL(
#version 150
in vec2 aPos;
in vec2 aUV;
out vec2 vUV;
void main() {
  gl_Position = vec4(aPos, 0.0, 1.0);
  vUV = aUV;
}
)GLSL";

const char* kFrameFs = R"GLSL(
#version 150
uniform sampler2D uTex;
in vec2 vUV;
out vec4 fragColor;
void main() {
  fragColor = texture(uTex, vUV);
}
)GLSL";

bool CompileProgram(GLuint& program)
{
  GLint status = 0;
  char log[512] = {0};

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &kFrameVs, nullptr);
  glCompileShader(vs);
  glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
  if (!status)
  {
    glGetShaderInfoLog(vs, sizeof(log), nullptr, log);
    fprintf(stderr, "frame vs: %s\n", log);
    return false;
  }

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &kFrameFs, nullptr);
  glCompileShader(fs);
  glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
  if (!status)
  {
    glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
    fprintf(stderr, "frame fs: %s\n", log);
    return false;
  }

  program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glBindAttribLocation(program, 0, "aPos");
  glBindAttribLocation(program, 1, "aUV");
  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status)
  {
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    fprintf(stderr, "frame program: %s\n", log);
    return false;
  }
  g_frame_tex_loc = glGetUniformLocation(program, "uTex");

  glDeleteShader(vs);
  glDeleteShader(fs);
  return true;
}

void PresentEnsure()
{
  if (g_frame_program)
    return;
  if (!CompileProgram(g_frame_program))
    return;

  // Fullscreen quad: positions and UVs.  The texture is uploaded top-row
  // first, so sample v=1 at the top of the screen.
  static const float vertices[] = {
      -1.0f, -1.0f, 0.0f, 0.0f,  // bottom-left
      +1.0f, -1.0f, 1.0f, 0.0f,  // bottom-right
      -1.0f, +1.0f, 0.0f, 1.0f,  // top-left
      +1.0f, +1.0f, 1.0f, 1.0f,  // top-right
  };

  glGenVertexArrays(1, &g_frame_vao);
  glBindVertexArray(g_frame_vao);
  glGenBuffers(1, &g_frame_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, g_frame_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
  glBindVertexArray(0);

  glGenTextures(1, &g_frame_texture);
  glBindTexture(GL_TEXTURE_2D, g_frame_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void ResetGlStateForUi()
{
  glUseProgram(0);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void PresentFrame()
{
  PresentEnsure();
  glViewport(0, 0, g_win_w, g_win_h);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  if (!g_frame_program)
  {
    SDL_GL_SwapWindow(g_window);
    return;
  }

  int w = vdp2width;
  int h = vdp2height;
  if (w > 0 && h > 0 && dispbuffer)
  {
    glBindTexture(GL_TEXTURE_2D, g_frame_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, dispbuffer);
    g_frame_w = w;
    g_frame_h = h;
  }
  else
  {
    glBindTexture(GL_TEXTURE_2D, g_frame_texture);
  }

  glUseProgram(g_frame_program);
  glUniform1i(g_frame_tex_loc, 0);
  glBindVertexArray(g_frame_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);

  SDL_GL_SwapWindow(g_window);
}
}  // namespace

//==============================================================================
// YUI callbacks the core invokes
//==============================================================================
extern "C" {

void YuiErrorMsg(const char* string)
{
  fprintf(stderr, "Yabause: %s\n", string);
}

void YuiSwapBuffers(void)
{
  static Uint32 last = 0;
  PresentFrame();
  Uint32 now = SDL_GetTicks();
  if (last != 0)
  {
    const Uint32 elapsed = now - last;
    if (elapsed < 16)
      SDL_Delay(16 - elapsed);
  }
  last = now;
}

int YuiRevokeOGLOnThisThread(void)
{
  return 0;
}

int YuiUseOGLOnThisThread(void)
{
  return 0;
}

// PlayRecorder feature hooks (implemented by the frontend thread layer).
int YabauseThread_IsUseBios(void)
{
  return 0;
}

const char* YabauseThread_getBackupPath(void)
{
  return "";
}

void YabauseThread_setUseBios(int use)
{
  (void)use;
}

void YabauseThread_setBackupPath(const char* path)
{
  (void)path;
}

void YabauseThread_resetPlaymode(void)
{
}

void YabauseThread_coldBoot(void)
{
}

// GL shader helpers referenced by the core init path; on the desktop build the
// GL stack is disabled so there is never a shader error to report.
char* getLastShaderError(void)
{
  return nullptr;
}
}

//==============================================================================
// Input
//==============================================================================
namespace
{
struct SaturnInput
{
  bool up = false, down = false, left = false, right = false;
  bool a = false, b = false, c = false;
  bool x = false, y = false, z = false;
  bool l = false, r = false;
  bool start = false;
  bool menu = false;    // back to boot page / exit
  bool quick_save = false;
  bool quick_load = false;
};

void ReadSaturnInput(SaturnInput& input)
{
  const Uint8* keys = SDL_GetKeyboardState(nullptr);
  input = SaturnInput{};
  input.up = keys[SDL_SCANCODE_UP] != 0;
  input.down = keys[SDL_SCANCODE_DOWN] != 0;
  input.left = keys[SDL_SCANCODE_LEFT] != 0;
  input.right = keys[SDL_SCANCODE_RIGHT] != 0;
  input.a = keys[SDL_SCANCODE_J] != 0;
  input.b = keys[SDL_SCANCODE_K] != 0;
  input.c = keys[SDL_SCANCODE_L] != 0;
  input.x = keys[SDL_SCANCODE_U] != 0;
  input.y = keys[SDL_SCANCODE_I] != 0;
  input.z = keys[SDL_SCANCODE_O] != 0;
  input.l = keys[SDL_SCANCODE_1] != 0;
  input.r = keys[SDL_SCANCODE_2] != 0;
  input.start = keys[SDL_SCANCODE_RETURN] != 0;

  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    switch (event.type)
    {
      case SDL_QUIT:
        g_quit = true;
        break;
      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_RESIZED)
        {
          g_win_w = event.window.data1;
          g_win_h = event.window.data2;
        }
        break;
      case SDL_KEYDOWN:
        switch (event.key.keysym.scancode)
        {
          case SDL_SCANCODE_ESCAPE:
            input.menu = true;
            break;
          case SDL_SCANCODE_F5:
            input.quick_save = true;
            break;
          case SDL_SCANCODE_F7:
            input.quick_load = true;
            break;
          default:
            break;
        }
        break;
      default:
        break;
    }
  }
}
}  // namespace

//==============================================================================
// Emulator
//==============================================================================
namespace
{
#define MAKE_PAD(a, b) ((a << 24) | (b))

int BootAndRunGame(const std::string& rom)
{
  const std::string backup = GBAStation::BackupPathForRom(rom);
  const std::string bios = GBAStation::FindSaturnBios();
  const bool use_emulated_bios =
      GBAStation::ReadConfigValue("core.saturn.emulated_bios", "0") == "1";

  yabauseinit_struct yinit = {};
  yinit.m68kcoretype = M68KCORE_MUSASHI;
  yinit.percoretype = PERCORE_DUMMY;
  yinit.sh2coretype = SH2CORE_INTERPRETER;
  yinit.vidcoretype = VIDCORE_SOFT;
  yinit.sndcoretype = SNDCORE_DUMMY;
  yinit.cdcoretype = CDCORE_ISO;
  yinit.carttype = CART_NONE;
  yinit.regionid = REGION_AUTODETECT;
  yinit.biospath = nullptr;
  if (!use_emulated_bios && !bios.empty())
  {
    yinit.biospath = bios.c_str();
  }
  yinit.cdpath = rom.c_str();
  yinit.buppath = backup.c_str();
  yinit.mpegpath = nullptr;
  yinit.cartpath = nullptr;
  yinit.videoformattype = VIDEOFORMATTYPE_NTSC;
  yinit.frameskip = std::atoi(GBAStation::ReadConfigValue("core.saturn.frame_skip", "0").c_str());
  yinit.usethreads = 0;
  yinit.skip_load = 0;
  yinit.use_new_scsp = 1;
  yinit.resolution_mode = std::atoi(GBAStation::ReadConfigValue("core.saturn.resolution_mode", "0").c_str());
  yinit.rotate_screen = 0;
  yinit.extend_backup = 0;
  yinit.scsp_main_mode = 1;
  yinit.scsp_sync_count_per_frame = 1;

  if (YabauseInit(&yinit) == -1)
  {
    fprintf(stderr, "YabauseInit failed for %s\n", rom.c_str());
    return 1;
  }

  void* padbits;
  PerPortReset();
  padbits = PerPadAdd(&PORTDATA1);
  PerSetKey(MAKE_PAD(0, PERPAD_UP), PERPAD_UP, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_RIGHT), PERPAD_RIGHT, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_DOWN), PERPAD_DOWN, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_LEFT), PERPAD_LEFT, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_START), PERPAD_START, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_A), PERPAD_A, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_B), PERPAD_B, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_C), PERPAD_C, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_X), PERPAD_X, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_Y), PERPAD_Y, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_Z), PERPAD_Z, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_RIGHT_TRIGGER), PERPAD_RIGHT_TRIGGER, padbits);
  PerSetKey(MAKE_PAD(0, PERPAD_LEFT_TRIGGER), PERPAD_LEFT_TRIGGER, padbits);

  PresentFrame();

  int result = 0;  // 0 = came back to boot page, 1 = quit application
  bool keep_running = true;
  while (keep_running && !g_quit)
  {
    SaturnInput input;
    ReadSaturnInput(input);

    if (input.menu)
    {
      result = 0;
      break;
    }
    if (input.quick_save)
      YabSaveState(GBAStation::StatePathForRom(rom, 1).c_str());
    if (input.quick_load)
      YabLoadState(GBAStation::StatePathForRom(rom, 1).c_str());

    if (input.up) PerKeyDown(MAKE_PAD(0, PERPAD_UP)); else PerKeyUp(MAKE_PAD(0, PERPAD_UP));
    if (input.down) PerKeyDown(MAKE_PAD(0, PERPAD_DOWN)); else PerKeyUp(MAKE_PAD(0, PERPAD_DOWN));
    if (input.left) PerKeyDown(MAKE_PAD(0, PERPAD_LEFT)); else PerKeyUp(MAKE_PAD(0, PERPAD_LEFT));
    if (input.right) PerKeyDown(MAKE_PAD(0, PERPAD_RIGHT)); else PerKeyUp(MAKE_PAD(0, PERPAD_RIGHT));
    if (input.a) PerKeyDown(MAKE_PAD(0, PERPAD_A)); else PerKeyUp(MAKE_PAD(0, PERPAD_A));
    if (input.b) PerKeyDown(MAKE_PAD(0, PERPAD_B)); else PerKeyUp(MAKE_PAD(0, PERPAD_B));
    if (input.c) PerKeyDown(MAKE_PAD(0, PERPAD_C)); else PerKeyUp(MAKE_PAD(0, PERPAD_C));
    if (input.x) PerKeyDown(MAKE_PAD(0, PERPAD_X)); else PerKeyUp(MAKE_PAD(0, PERPAD_X));
    if (input.y) PerKeyDown(MAKE_PAD(0, PERPAD_Y)); else PerKeyUp(MAKE_PAD(0, PERPAD_Y));
    if (input.z) PerKeyDown(MAKE_PAD(0, PERPAD_Z)); else PerKeyUp(MAKE_PAD(0, PERPAD_Z));
    if (input.l) PerKeyDown(MAKE_PAD(0, PERPAD_LEFT_TRIGGER)); else PerKeyUp(MAKE_PAD(0, PERPAD_LEFT_TRIGGER));
    if (input.r) PerKeyDown(MAKE_PAD(0, PERPAD_RIGHT_TRIGGER)); else PerKeyUp(MAKE_PAD(0, PERPAD_RIGHT_TRIGGER));
    if (input.start) PerKeyDown(MAKE_PAD(0, PERPAD_START)); else PerKeyUp(MAKE_PAD(0, PERPAD_START));

    YabauseExec();
  }

  YabauseDeInit();
  ResetGlStateForUi();
  return result;
}
}  // namespace

//==============================================================================
// Boot page
//==============================================================================
namespace
{
NVGcontext* g_ui_vg = nullptr;
GbBoot::UiFontSet g_ui_fonts;
std::string g_exe_dir;

bool FileExists(const std::string& path)
{
  FILE* file = fopen(path.c_str(), "rb");
  if (!file)
    return false;
  fclose(file);
  return true;
}

// Finds a directory that contains font.ttf + switch_icons.ttf +
// MaterialIcons-Regular.ttf (the repo's res/font).  Returns "" when not found.
std::string FindFontDir()
{
  std::vector<std::string> candidates;
  const char* res_root = getenv("GBASTATION_RES_ROOT");
  if (res_root && res_root[0])
    candidates.push_back(res_root);
  const std::string data_root = GBAStation::DataRoot();
  candidates.push_back(data_root + "/res/font");
  candidates.push_back(data_root + "/font");
  if (!g_exe_dir.empty())
    candidates.push_back(g_exe_dir + "/../../../res/font");
  candidates.push_back("./res/font");

  for (const std::string& dir : candidates)
  {
    if (FileExists(dir + "/font.ttf"))
      return dir;
  }
  return "";
}

bool EnsureUi()
{
  if (g_ui_vg)
    return true;
  g_ui_vg = UiCreateGL3Context();
  if (!g_ui_vg)
    return false;
  const std::string font_dir = FindFontDir();
  return GbBoot::UiFontsLoad(g_ui_vg, g_ui_fonts,
                             font_dir.empty() ? nullptr : font_dir.c_str());
}

void ShowLaunchOverlay(const std::string& rom)
{
  if (!g_ui_vg)
    return;
  const auto slash = rom.find_last_of("/\\");
  const std::string name = slash == std::string::npos ? rom : rom.substr(slash + 1);
  glViewport(0, 0, g_win_w, g_win_h);
  GbBoot::NvgUiDrawLaunchOverlay(g_ui_vg, g_ui_fonts, g_win_w, g_win_h, name.c_str());
  SDL_GL_SwapWindow(g_window);
}

struct BrowserKeys
{
  bool up = false, down = false;
  bool confirm = false;
  bool back = false;
  bool toggle_games = false;
  bool cycle_sort = false;
  bool quit = false;
};

void ReadBrowserKeys(BrowserKeys& keys)
{
  const Uint8* state = SDL_GetKeyboardState(nullptr);
  static bool prev_confirm = false;
  static bool prev_back = false;
  static bool prev_games = false;
  static bool prev_sort = false;
  keys = BrowserKeys{};
  keys.up = state[SDL_SCANCODE_UP] != 0;
  keys.down = state[SDL_SCANCODE_DOWN] != 0;

  const bool confirm_now = state[SDL_SCANCODE_RETURN] != 0 || state[SDL_SCANCODE_RIGHT] != 0;
  const bool back_now = state[SDL_SCANCODE_BACKSPACE] != 0 || state[SDL_SCANCODE_LEFT] != 0;
  const bool games_now = state[SDL_SCANCODE_Y] != 0;
  const bool sort_now = state[SDL_SCANCODE_X] != 0;
  keys.confirm = confirm_now && !prev_confirm;
  keys.back = back_now && !prev_back;
  keys.toggle_games = games_now && !prev_games;
  keys.cycle_sort = sort_now && !prev_sort;
  prev_confirm = confirm_now;
  prev_back = back_now;
  prev_games = games_now;
  prev_sort = sort_now;

  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    switch (event.type)
    {
      case SDL_QUIT:
        g_quit = true;
        break;
      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_RESIZED)
        {
          g_win_w = event.window.data1;
          g_win_h = event.window.data2;
        }
        break;
      case SDL_KEYDOWN:
        if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
          keys.quit = true;
        break;
      default:
        break;
    }
  }
}

struct RepeatState
{
  bool held = false;
  Uint32 next = 0;
};

void UpdateRepeat(bool held_now, Uint32 now, RepeatState& state, bool& fire)
{
  fire = false;
  if (!held_now)
  {
    state.held = false;
    return;
  }
  if (!state.held)
  {
    state.held = true;
    state.next = now + 280;
    fire = true;
  }
  else if (now >= state.next)
  {
    state.next = now + 130;
    fire = true;
  }
}

int RunBootPage()
{
  GbBoot::DirBrowser browser;
  std::string status;

  std::string root = GBAStation::ReadConfigValue("core.saturn.rom_dir");
  if (root.empty())
    root = GBAStation::DefaultRomsDir();
  std::string err;
  if (!browser.Open(root, err))
  {
    root = GBAStation::DefaultRomsDir();
    if (!browser.Open(root, err))
    {
      status = "Cannot open " + GBAStation::DefaultRomsDir();
      fprintf(stderr, "%s\n", status.c_str());
    }
  }

  EnsureUi();

  while (!g_quit)
  {
    BrowserKeys keys;
    ReadBrowserKeys(keys);

    if (keys.quit)
      break;

    if (keys.toggle_games)
      browser.SetGamesOnly(!browser.GamesOnly());
    if (keys.cycle_sort)
      browser.CycleSort();

    const Uint32 now = SDL_GetTicks();
    static RepeatState repeat_up;
    static RepeatState repeat_down;
    bool up = false;
    bool down = false;
    UpdateRepeat(keys.up, now, repeat_up, up);
    UpdateRepeat(keys.down, now, repeat_down, down);
    if (up) browser.MoveCursor(-1);
    if (down) browser.MoveCursor(1);

    if (keys.confirm && browser.Activate())
    {
      std::string rom = browser.Selected();
      ResetGlStateForUi();
      ShowLaunchOverlay(rom);
      const int outcome = BootAndRunGame(rom);
      if (outcome != 0 || g_quit)
        break;
      std::string open_err;
      browser.Open(GbBoot::ParentOf(rom), open_err);
      status.clear();
    }
    if (keys.back)
      browser.GoParent();

    glViewport(0, 0, g_win_w, g_win_h);
    glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (g_ui_vg)
    {
      GbBoot::NvgUiDrawBrowser(g_ui_vg, browser, g_ui_fonts, g_win_w, g_win_h,
                               status.empty() ? nullptr : status.c_str());
    }
    SDL_GL_SwapWindow(g_window);
    SDL_Delay(16);
  }

  return 0;
}
}  // namespace

//==============================================================================
int main(int argc, char** argv)
{
  if (argc > 0 && argv[0] && argv[0][0])
  {
    const std::string self(argv[0]);
    const auto slash = self.find_last_of("/\\");
    if (slash != std::string::npos)
      g_exe_dir = self.substr(0, slash);
  }

  GBAStation::LaunchInfo launch = GBAStation::ReadLaunchInfo(argc, argv);

  if (SDL_Init(SDL_INIT_VIDEO) != 0)
  {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  g_window = SDL_CreateWindow("GBAStation YabaSanshiro", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 1280, 720,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
  if (!g_window)
  {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  g_glcontext = SDL_GL_CreateContext(g_window);
  if (!g_glcontext)
  {
    fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_GL_MakeCurrent(g_window, g_glcontext);
  SDL_GL_SetSwapInterval(1);
  SDL_GetWindowSize(g_window, &g_win_w, &g_win_h);

  GBAStation::EnsureSaturnDirectories();

  int result = 0;
  if (!launch.rom_path.empty())
  {
    EnsureUi();
    ShowLaunchOverlay(launch.rom_path);
    BootAndRunGame(launch.rom_path);
  }
  else
  {
    result = RunBootPage();
  }

  if (g_ui_vg)
  {
    nvgDeleteGL3(g_ui_vg);
    g_ui_vg = nullptr;
  }
  if (g_glcontext)
    SDL_GL_DeleteContext(g_glcontext);
  if (g_window)
    SDL_DestroyWindow(g_window);
  SDL_Quit();
  return result;
}
