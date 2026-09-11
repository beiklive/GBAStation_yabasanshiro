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

// Shared storage root for everything the core reads and writes.
//   * Nintendo Switch : "sdmc:/GBAStation"
//   * other platforms  : $GBASTATION_ROOT or ./GBAStation (portable override)
std::string DataRoot();

LaunchInfo ReadLaunchInfo(int argc, char** argv);
void ReturnToLauncher(const LaunchInfo& launch);
void EnsureSaturnDirectories();
std::string DefaultRomsDir();
std::string FindSaturnBios();
std::string BackupPathForRom(const std::string& rom_path);
std::string StatePathForRom(const std::string& rom_path, unsigned slot);
std::string ReadConfigValue(const char* key, const char* fallback = "");

// Config values use the same PAD_* names written by BeikLiveStation.
// The bit masks are libnx HidNpadButton values and are only meaningful on
// the Nintendo Switch frontend.
uint64_t ReadButtonMapping(const char* key, uint64_t fallback);
bool IsButtonMappingPressed(const char* key, uint64_t held, uint64_t fallback);
}
