#include "GBAStationPlatform.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sys/stat.h>

#include <switch.h>

namespace
{
constexpr const char* kConfigPath = "sdmc:/GBAStation/config/config.cfg";

std::map<std::string, std::string> ReadConfig()
{
  std::map<std::string, std::string> values;
  std::ifstream file(kConfigPath);
  std::string line;
  while (std::getline(file, line))
  {
    const auto comment = line.find_first_of("#;");
    if (comment != std::string::npos)
      line.resize(comment);
    const auto equal = line.find('=');
    if (equal == std::string::npos)
      continue;
    auto trim = [](std::string value) {
      const auto first = value.find_first_not_of(" \t\r\n");
      if (first == std::string::npos)
        return std::string{};
      const auto last = value.find_last_not_of(" \t\r\n");
      return value.substr(first, last - first + 1);
    };
    values[trim(line.substr(0, equal))] = trim(line.substr(equal + 1));
  }
  return values;
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
    if (arg == "--gbastation-session" || arg == "--tico-rom")
    {
      if (++i < argc && argv[i] && argv[i][0])
      {
        if (arg == "--gbastation-session")
          info.session_token = argv[i];
        else if (info.rom_path.empty())
          info.rom_path = argv[i];
      }
      continue;
    }
    if (arg.rfind("--", 0) == 0)
      continue;
    if (info.rom_path.empty())
      info.rom_path = arg;
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
  std::string name = rom_path;
  const auto slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name.erase(0, slash + 1);
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos) name.erase(dot);
  for (char& c : name)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
  return "sdmc:/GBAStation/saturn/states/" + name + ".state" + std::to_string(slot);
}

std::string ReadConfigValue(const char* key, const char* fallback)
{
  const auto values = ReadConfig();
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
