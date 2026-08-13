#pragma once

#include <cstdint>
#include <string>

namespace GBAStation
{
struct LaunchInfo
{
  std::string rom_path;
  std::string return_nro;
  std::string session_token;
};

LaunchInfo ReadLaunchInfo(int argc, char** argv);
void ReturnToLauncher(const LaunchInfo& launch);
void EnsureSaturnDirectories();
std::string FindSaturnBios();
std::string StatePathForRom(const std::string& rom_path, unsigned slot);
std::string ReadConfigValue(const char* key, const char* fallback = "");

// Config values use the same PAD_* names written by BeikLiveStation.
uint64_t ReadButtonMapping(const char* key, uint64_t fallback);
bool IsButtonMappingPressed(const char* key, uint64_t held, uint64_t fallback);
}
