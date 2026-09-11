#pragma once

#include <map>
#include <string>
#include <vector>

// Core settings model for the built-in settings page.  Values are strings as
// stored in config.cfg (BeikLiveStation style); the page cycles/enum changes
// them and SaveSettings() writes the whole config back.
namespace GbBoot
{
enum class SettingType
{
  kBool,
  kChoice,
  kText,  // displayed read-only (paths)
};

struct SettingItem
{
  std::string key;
  std::string label;
  std::string value;
  SettingType type = SettingType::kBool;
  // For kChoice: "value|label" entries, in cycle order.
  std::vector<std::string> options;
  bool editable = true;

  std::string Display() const;
};

// Builds the full list of core settings from the loaded config (missing keys
// fall back to the same defaults the core uses).
std::vector<SettingItem> BuildCoreSettings(const std::map<std::string, std::string>& cfg);

// direction +1 / -1: cycle choice / toggle bool.
void ChangeSetting(SettingItem& item, int direction);

// Writes key=value lines, preserving unknown keys already present in the file.
bool SaveSettings(const std::string& path, const std::vector<SettingItem>& items);
}
