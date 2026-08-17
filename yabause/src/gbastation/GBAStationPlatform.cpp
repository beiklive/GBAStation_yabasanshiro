#include "GBAStationPlatform.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <string_view>

#include <switch.h>

namespace
{
constexpr const char* kConfigPaths[] = {
    "sdmc:/GBAStation/config/config.cfg",
    "/GBAStation/config/config.cfg",
};

std::string Trim(std::string_view value)
{
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

std::string DecodeConfigValue(std::string_view value)
{
  std::string decoded = Trim(value);
  // BeikLiveStation persists scalar values as i|, f| or s|.  Decode the
  // transport representation here so all consumers see the plain value.
  if (decoded.rfind("i|", 0) == 0 || decoded.rfind("f|", 0) == 0 ||
      decoded.rfind("s|", 0) == 0)
    decoded.erase(0, 2);

  std::string unescaped;
  unescaped.reserve(decoded.size());
  bool escaped = false;
  for (const char character : decoded)
  {
    if (escaped)
    {
      unescaped.push_back(character);
      escaped = false;
    }
    else if (character == '\\')
    {
      escaped = true;
    }
    else
    {
      unescaped.push_back(character);
    }
  }
  if (escaped)
    unescaped.push_back('\\');
  return unescaped;
}

const std::map<std::string, std::string>& ReadConfig()
{
  static const std::map<std::string, std::string> values = [] {
    std::map<std::string, std::string> loaded;
    for (const char* path : kConfigPaths)
    {
      std::ifstream file(path);
      if (!file)
        continue;

      std::string line;
      while (std::getline(file, line))
      {
        const auto comment = line.find_first_of("#;");
        if (comment != std::string::npos)
          line.resize(comment);
        const auto equal = line.find('=');
        if (equal == std::string::npos)
          continue;
        loaded[Trim(std::string_view(line).substr(0, equal))] =
            DecodeConfigValue(std::string_view(line).substr(equal + 1));
      }
      break;
    }
    return loaded;
  }();
  return values;
}

std::string SanitizedRomName(const std::string& rom_path)
{
  std::string name = rom_path;
  const auto slash = name.find_last_of("/\\");
  if (slash != std::string::npos)
    name.erase(0, slash + 1);
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos)
    name.erase(dot);
  for (char& c : name)
  {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
      c = '_';
  }
  return name.empty() ? "saturn" : name;
}

uint32_t StablePathHash(std::string_view path)
{
  uint32_t hash = 2166136261u;
  for (const unsigned char character : path)
  {
    hash ^= character;
    hash *= 16777619u;
  }
  return hash;
}

std::string RomStorageName(const std::string& rom_path)
{
  char suffix[9];
  std::snprintf(suffix, sizeof(suffix), "%08x", StablePathHash(rom_path));
  return SanitizedRomName(rom_path) + "-" + suffix;
}

bool ReadJsonString(std::string_view text, const char* key, std::string& value)
{
  const std::string needle = std::string("\"") + key + "\"";
  const size_t key_position = text.find(needle);
  if (key_position == std::string_view::npos)
    return false;

  const size_t colon = text.find(':', key_position + needle.size());
  const size_t quote = colon == std::string_view::npos ? std::string_view::npos :
      text.find('"', colon + 1);
  if (quote == std::string_view::npos)
    return false;

  value.clear();
  bool escaped = false;
  for (size_t index = quote + 1; index < text.size(); ++index)
  {
    const char character = text[index];
    if (escaped)
    {
      value.push_back(character);
      escaped = false;
    }
    else if (character == '\\')
    {
      escaped = true;
    }
    else if (character == '"')
    {
      return true;
    }
    else
    {
      value.push_back(character);
    }
  }
  return false;
}

bool LoadLaunchFile(const char* path, GBAStation::LaunchInfo& info)
{
  std::ifstream file(path);
  if (!file)
    return false;

  std::ostringstream stream;
  stream << file.rdbuf();
  const std::string json = stream.str();
  static constexpr const char* kContentKeys[] = {
      "contentPath", "content_path", "romPath", "rom", "path", "gamePath"};
  for (const char* key : kContentKeys)
  {
    if (ReadJsonString(json, key, info.rom_path) && !info.rom_path.empty())
      break;
  }
  if (info.rom_path.empty())
    return false;

  if (info.session_token.empty())
    ReadJsonString(json, "sessionToken", info.session_token);
  return true;
}

uint64_t ParsePadName(std::string name, uint64_t fallback)
{
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  static const std::map<std::string, uint64_t> names = {
      {"PAD_A", HidNpadButton_A}, {"PAD_B", HidNpadButton_B},
      {"PAD_X", HidNpadButton_X}, {"PAD_Y", HidNpadButton_Y},
      {"PAD_LB", HidNpadButton_L}, {"PAD_RB", HidNpadButton_R},
      {"PAD_LT", HidNpadButton_ZL}, {"PAD_RT", HidNpadButton_ZR},
      {"PAD_START", HidNpadButton_Plus}, {"PAD_BACK", HidNpadButton_Minus},
      {"PAD_UP", HidNpadButton_Up}, {"PAD_DOWN", HidNpadButton_Down},
      {"PAD_LEFT", HidNpadButton_Left}, {"PAD_RIGHT", HidNpadButton_Right},
      {"PAD_LSB", HidNpadButton_StickL}, {"PAD_RSB", HidNpadButton_StickR}};
  uint64_t result = 0;
  size_t start = 0;
  while (start <= name.size())
  {
    const size_t plus = name.find('+', start);
    const std::string token = name.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
    const auto it = names.find(token);
    if (it == names.end())
      return fallback;
    result |= it->second;
    if (plus == std::string::npos)
      break;
    start = plus + 1;
  }
  return result == 0 ? fallback : result;
}

void MakeDirectory(const char* path)
{
  mkdir(path, 0777);
}
}

namespace GBAStation
{
LaunchInfo ReadLaunchInfo(int argc, char** argv)
{
  LaunchInfo info;
  for (int i = 1; i < argc; ++i)
  {
    if (!argv[i] || !argv[i][0])
      continue;
    const std::string arg(argv[i]);
    if (arg == "--return")
    {
      if (++i < argc && argv[i] && argv[i][0]) info.return_nro = argv[i];
      continue;
    }
    if (arg == "--gbastation-session")
    {
      if (++i < argc && argv[i] && argv[i][0])
        info.session_token = argv[i];
      continue;
    }
    if (arg == "--launch")
    {
      if (++i < argc && argv[i] && argv[i][0])
        LoadLaunchFile(argv[i], info);
      continue;
    }
    // Kept only for existing Tico launchers.  New callers pass a positional
    // path or --launch <json> using the shared GBAStation protocol.
    if (arg == "--tico-rom")
    {
      if (++i < argc && argv[i] && argv[i][0] && info.rom_path.empty())
        info.rom_path = argv[i];
      continue;
    }
    if (arg.rfind("--", 0) == 0)
      continue;
    if (info.rom_path.empty())
      info.rom_path = arg;
  }

  if (info.rom_path.empty())
  {
    static constexpr const char* kLaunchFiles[] = {
        "sdmc:/GBAStation/runtime/saturn_launch.json",
        "sdmc:/GBAStation/runtime/launch.json",
        "sdmc:/GBAStation/launch.json",
    };
    for (const char* path : kLaunchFiles)
    {
      if (LoadLaunchFile(path, info))
        break;
    }
  }
  return info;
}

void ReturnToLauncher(const LaunchInfo& launch)
{
  if (launch.return_nro.empty() || !envHasNextLoad())
    return;

  const std::string args = launch.return_nro +
      (launch.session_token.empty() ? " --resume" : " --external-return " + launch.session_token);
  envSetNextLoad(launch.return_nro.c_str(), args.c_str());
}

void EnsureSaturnDirectories()
{
  MakeDirectory("sdmc:/GBAStation");
  MakeDirectory("sdmc:/GBAStation/saturn");
  MakeDirectory("sdmc:/GBAStation/saturn/backup");
  MakeDirectory("sdmc:/GBAStation/saturn/states");
  MakeDirectory("sdmc:/GBAStation/saturn/cache");
  MakeDirectory("sdmc:/GBAStation/saturn/cache/shaders");
  MakeDirectory("sdmc:/GBAStation/saturn/cache/pipelines");
}

std::string FindSaturnBios()
{
  constexpr const char* bios[] = {"saturn_bios.bin", "sega_101.bin", "mpr-17933.bin"};
  for (const char* name : bios)
  {
    const std::string path = std::string("sdmc:/GBAStation/bios/saturn/") + name;
    std::ifstream test(path, std::ios::binary);
    if (test.good())
      return path;
  }
  return {};
}

std::string StatePathForRom(const std::string& rom_path, unsigned slot)
{
  return "sdmc:/GBAStation/saturn/states/" + RomStorageName(rom_path) + ".state" +
      std::to_string(slot);
}

std::string BackupPathForRom(const std::string& rom_path)
{
  return "sdmc:/GBAStation/saturn/backup/" + RomStorageName(rom_path) + ".ram";
}

std::string ReadConfigValue(const char* key, const char* fallback)
{
  const auto& values = ReadConfig();
  const auto it = values.find(key);
  return it == values.end() ? std::string(fallback) : it->second;
}

uint64_t ReadButtonMapping(const char* key, uint64_t fallback)
{
  return ParsePadName(ReadConfigValue(key).c_str(), fallback);
}

bool IsButtonMappingPressed(const char* key, uint64_t held, uint64_t fallback)
{
  const uint64_t switch_key = ReadButtonMapping(key, fallback);
  return switch_key != 0 && (held & switch_key) == switch_key;
}
}
