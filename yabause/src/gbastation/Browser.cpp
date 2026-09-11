#include "Browser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace GbBoot
{
namespace
{
bool EndsWithIgnoreCase(const std::string& value, const char* suffix)
{
  const size_t suffix_len = std::strlen(suffix);
  if (value.size() < suffix_len)
    return false;
  for (size_t index = 0; index < suffix_len; ++index)
  {
    if (std::tolower(static_cast<unsigned char>(value[value.size() - suffix_len + index])) !=
        static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[index]))))
      return false;
  }
  return true;
}

int CaseInsensitiveCompare(const std::string& a, const std::string& b)
{
  const size_t count = std::min(a.size(), b.size());
  for (size_t index = 0; index < count; ++index)
  {
    const int ca = std::tolower(static_cast<unsigned char>(a[index]));
    const int cb = std::tolower(static_cast<unsigned char>(b[index]));
    if (ca != cb)
      return ca < cb ? -1 : 1;
  }
  if (a.size() == b.size())
    return 0;
  return a.size() < b.size() ? -1 : 1;
}

std::string ExtensionUpper(const std::string& name)
{
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos || dot + 1 >= name.size())
    return "";
  std::string ext = name.substr(dot + 1);
  for (char& c : ext)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return ext;
}
}  // namespace

bool IsSupportedGameFile(const std::string& name)
{
  return EndsWithIgnoreCase(name, ".cue") || EndsWithIgnoreCase(name, ".iso") ||
         EndsWithIgnoreCase(name, ".mds") || EndsWithIgnoreCase(name, ".ccd") ||
         EndsWithIgnoreCase(name, ".chd") || EndsWithIgnoreCase(name, ".bin");
}

// Joins a directory and a child name without producing "//" when the
// directory already ends in a slash (e.g. the "sdmc:/" device root).
std::string ChildPath(const std::string& dir, const std::string& name)
{
  if (dir.empty())
    return name;
  if (dir.back() == '/')
    return dir + name;
  return dir + "/" + name;
}

std::string ParentOf(const std::string& path)
{
  std::string copy = path;
  while (copy.size() > 1 && copy.back() == '/')
    copy.pop_back();

  const auto slash = copy.find_last_of('/');
  if (slash == std::string::npos || slash == 0)
  {
    if (copy == "/")
      return "/";
    // Device root style ("sdmc:") keeps a trailing slash so opendir works.
    if (copy.find(':') != std::string::npos)
    {
      if (copy.back() != '/')
        copy.push_back('/');
      return copy;
    }
    return path;
  }

  std::string parent = copy.substr(0, slash);
  if (parent.find('/') == std::string::npos && parent.find(':') != std::string::npos)
    parent.push_back('/');
  return parent;
}

bool DirBrowser::Open(const std::string& path, std::string& error)
{
  DIR* dir = opendir(path.c_str());
  if (!dir)
  {
    error = "cannot open directory";
    return false;
  }

  std::vector<DirEntry> listing;
  while (struct dirent* entry = readdir(dir))
  {
    const std::string name(entry->d_name);
    if (name.empty() || name[0] == '.')
      continue;

    DirEntry item;
    item.name = name;
    const std::string full = ChildPath(path, name);

    // Prefer the dirent type so entries are never dropped when stat() fails
    // (some filesystems return errors for otherwise valid directory entries).
    bool known_dir = false;
#if defined(DT_DIR)
    if (entry->d_type == DT_DIR)
      known_dir = true;
#endif
    struct stat info;
    const bool have_stat = (stat(full.c_str(), &info) == 0);
    item.is_dir = known_dir || (have_stat && S_ISDIR(info.st_mode));
    item.is_game = !item.is_dir && IsSupportedGameFile(name);
    if (have_stat)
    {
      item.size = item.is_dir ? 0 : static_cast<int64_t>(info.st_size);
      item.mtime = static_cast<int64_t>(info.st_mtime);
    }
    listing.push_back(item);
  }
  closedir(dir);

  directory_ = path;
  selected_.clear();
  can_go_parent_ = (path != "/") && (ParentOf(path) != path);

  // Virtual "parent" row shown first when a parent exists.  Filtering and
  // sorting never move it (see RefreshView).
  all_entries_.clear();
  if (can_go_parent_)
  {
    DirEntry parent;
    parent.name = "..";
    parent.is_parent = true;
    all_entries_.push_back(parent);
  }
  all_entries_.insert(all_entries_.end(), listing.begin(), listing.end());

  RefreshView();
  return true;
}

void DirBrowser::RefreshView()
{
  entries_ = all_entries_;
  if (games_only_)
  {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [](const DirEntry& e) {
                                    return !e.is_parent && !e.is_dir && !e.is_game;
                                  }),
                   entries_.end());
  }

  std::sort(entries_.begin(), entries_.end(), [this](const DirEntry& a, const DirEntry& b) {
    if (a.is_parent != b.is_parent)
      return a.is_parent;  // parent row stays first
    if (a.is_dir != b.is_dir)
      return a.is_dir;  // directories first
    switch (sort_mode_)
    {
      case SortMode::kSize:
        if (a.size != b.size)
          return a.size > b.size;
        break;
      case SortMode::kTime:
        if (a.mtime != b.mtime)
          return a.mtime > b.mtime;
        break;
      case SortMode::kName:
      default:
        break;
    }
    const int cmp = CaseInsensitiveCompare(a.name, b.name);
    if (cmp != 0)
      return cmp < 0;
    return a.name < b.name;
  });

  ClampCursor();
}

void DirBrowser::ClampCursor()
{
  if (entries_.empty())
  {
    cursor_ = 0;
    return;
  }
  if (cursor_ >= entries_.size())
    cursor_ = entries_.size() - 1;
}

bool DirBrowser::GoParent()
{
  const std::string parent = ParentOf(directory_);
  if (parent == directory_)
    return false;
  std::string error;
  if (!Open(parent, error))
    return false;
  return true;
}

void DirBrowser::MoveCursor(int delta)
{
  if (entries_.empty())
    return;
  int cursor = static_cast<int>(cursor_);
  cursor += delta;
  if (cursor < 0)
    cursor = 0;
  if (cursor >= static_cast<int>(entries_.size()))
    cursor = static_cast<int>(entries_.size()) - 1;
  cursor_ = static_cast<size_t>(cursor);
}

void DirBrowser::SetGamesOnly(bool enabled)
{
  if (games_only_ == enabled)
    return;
  games_only_ = enabled;
  RefreshView();
}

void DirBrowser::CycleSort()
{
  switch (sort_mode_)
  {
    case SortMode::kName:
      sort_mode_ = SortMode::kSize;
      break;
    case SortMode::kSize:
      sort_mode_ = SortMode::kTime;
      break;
    case SortMode::kTime:
    default:
      sort_mode_ = SortMode::kName;
      break;
  }
  RefreshView();
}

bool DirBrowser::Activate()
{
  if (cursor_ >= entries_.size())
    return false;
  const DirEntry& item = entries_[cursor_];
  if (item.is_parent)
  {
    // "return to parent" row.
    GoParent();
    return false;
  }
  if (item.is_dir)
  {
    std::string error;
    if (!Open(ChildPath(directory_, item.name), error))
      return false;
    return false;
  }
  if (item.is_game)
  {
    selected_ = ChildPath(directory_, item.name);
    return true;
  }
  return false;
}
}
