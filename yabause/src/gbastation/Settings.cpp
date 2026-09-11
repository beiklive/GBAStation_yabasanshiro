#include "Settings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace GbBoot
{
namespace
{
std::string ValueOf(const std::map<std::string, std::string>& cfg, const char* key,
                    const char* fallback)
{
  const auto it = cfg.find(key);
  return it == cfg.end() || it->second.empty() ? std::string(fallback) : it->second;
}

void AddBool(std::vector<SettingItem>& out, const std::map<std::string, std::string>& cfg,
             const char* key, const char* label, const char* fallback)
{
  SettingItem item;
  item.key = key;
  item.label = label;
  item.type = SettingType::kBool;
  item.value = ValueOf(cfg, key, fallback);
  out.push_back(item);
}

void AddChoice(std::vector<SettingItem>& out,
               const std::map<std::string, std::string>& cfg, const char* key,
               const char* label, const char* fallback,
               std::vector<std::string> options)
{
  SettingItem item;
  item.key = key;
  item.label = label;
  item.type = SettingType::kChoice;
  item.value = ValueOf(cfg, key, fallback);
  item.options = std::move(options);
  out.push_back(item);
}

void AddText(std::vector<SettingItem>& out, const std::map<std::string, std::string>& cfg,
             const char* key, const char* label)
{
  SettingItem item;
  item.key = key;
  item.label = label;
  item.type = SettingType::kText;
  item.value = ValueOf(cfg, key, "");
  item.editable = false;
  out.push_back(item);
}
}  // namespace

std::string SettingItem::Display() const
{
  if (type == SettingType::kBool)
    return (value == "1" || value == "true" || value == "on") ? "开" : "关";
  if (type == SettingType::kChoice)
  {
    for (const std::string& option : options)
    {
      const size_t bar = option.find('|');
      const std::string opt_value = option.substr(0, bar);
      if (opt_value == value)
        return bar == std::string::npos ? option : option.substr(bar + 1);
    }
    return value;
  }
  return value;
}

std::vector<SettingItem> BuildCoreSettings(const std::map<std::string, std::string>& cfg)
{
  std::vector<SettingItem> items;
  AddChoice(items, cfg, "core.saturn.resolution_mode", "内部分辨率", "0",
            {"0|原生", "1|4x", "2|2x", "3|原始"});
  AddBool(items, cfg, "core.saturn.aspect_ratio", "保持宽高比", "0");
  AddBool(items, cfg, "core.saturn.frame_skip", "自动跳帧", "0");
  AddChoice(items, cfg, "core.saturn.frame_limit", "帧率限制", "0",
            {"0|60fps", "1|不限", "2|120fps"});
  AddChoice(items, cfg, "core.saturn.video_filter", "视频滤镜", "0",
            {"0|关闭", "1|FXAA", "2|扫描线"});
  AddChoice(items, cfg, "core.saturn.polygon_generation", "多边形生成", "0",
            {"0|透视校正", "1|GPU 细分"});
  AddChoice(items, cfg, "core.saturn.rbg_resolution", "RBG 分辨率", "0",
            {"0|原生", "1|高分辨率"});
  AddBool(items, cfg, "core.saturn.rbg_compute_shader", "RBG 计算着色器", "0");
  AddBool(items, cfg, "core.saturn.extend_internal_memory", "扩展内部内存", "0");
  AddBool(items, cfg, "core.saturn.rotate_screen", "画面旋转", "0");
  AddChoice(items, cfg, "core.saturn.scsp_sync_per_frame", "SCSP 同步次数", "1",
            {"1|1", "2|2", "4|4", "8|8"});
  AddChoice(items, cfg, "core.saturn.scsp_sync_time_mode", "SCSP 同步模式", "1",
            {"0|按 CPU", "1|按时间"});
  AddChoice(items, cfg, "core.saturn.sound_engine", "声音引擎", "1",
            {"0|旧版", "1|新版"});
  AddChoice(items, cfg, "core.saturn.cartridge", "卡带类型", "0",
            {"0|无", "1|4M 备份", "2|8M 备份", "3|16M 备份", "4|32M 备份",
             "5|DRAM 8M", "6|DRAM 32M", "7|Netlink", "8|ROM 16M"});
  AddChoice(items, cfg, "core.saturn.region", "区域", "0",
            {"0|自动", "1|日本", "2|亚洲 NTSC", "4|北美", "5|中南美 NTSC",
             "6|韩国", "10|亚洲 PAL", "12|欧洲", "13|中南美 PAL"});
  AddChoice(items, cfg, "core.saturn.video_format", "视频制式", "0",
            {"0|NTSC", "1|PAL"});
  AddBool(items, cfg, "core.saturn.emulated_bios", "使用模拟 BIOS", "0");
  AddChoice(items, cfg, "core.saturn.cpu_sync_per_line", "CPU 同步/行", "1",
            {"1|1", "2|2", "4|4", "8|8"});
  AddText(items, cfg, "core.saturn.biosPath", "BIOS 路径");
  AddText(items, cfg, "core.saturn.savePath", "存档路径");
  return items;
}

void ChangeSetting(SettingItem& item, int direction)
{
  if (!item.editable)
    return;
  if (item.type == SettingType::kBool)
  {
    const bool on = (item.value == "1" || item.value == "true" || item.value == "on");
    item.value = on ? "0" : "1";
    return;
  }
  if (item.type == SettingType::kChoice && !item.options.empty())
  {
    size_t index = 0;
    for (size_t i = 0; i < item.options.size(); ++i)
    {
      const size_t bar = item.options[i].find('|');
      if (item.options[i].substr(0, bar) == item.value)
      {
        index = i;
        break;
      }
    }
    const int count = static_cast<int>(item.options.size());
    int next = static_cast<int>(index) + (direction >= 0 ? 1 : -1);
    next = (next % count + count) % count;
    const size_t bar = item.options[next].find('|');
    item.value = item.options[next].substr(0, bar);
  }
}

bool SaveSettings(const std::string& path, const std::vector<SettingItem>& items)
{
  // Preserve unknown keys that are already in the file.
  std::map<std::string, std::string> values;
  {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
    {
      const size_t eq = line.find('=');
      if (eq == std::string::npos)
        continue;
      std::string key = line.substr(0, eq);
      std::string value = line.substr(eq + 1);
      while (!key.empty() && (key.back() == ' ' || key.back() == '\t' || key.back() == '\r'))
        key.pop_back();
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.erase(value.begin());
      while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.pop_back();
      if (!key.empty())
        values[key] = value;
    }
  }
  for (const SettingItem& item : items)
    values[item.key] = item.value;

  std::ofstream out(path, std::ios::trunc);
  if (!out)
    return false;
  for (const auto& entry : values)
    out << entry.first << "=" << entry.second << "\n";
  return out.good();
}
}
