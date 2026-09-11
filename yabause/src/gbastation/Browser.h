#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Portable directory browser used by the built-in boot page.  It has no
// dependency on the platform: rendering and input live in the frontends, this
// only keeps the listing, navigation, filtering, sorting and selection state.
namespace GbBoot
{
bool IsSupportedGameFile(const std::string& name);

// Returns the parent directory of path, or path itself when there is no
// usable parent (filesystem root / device root like "sdmc:/").
std::string ParentOf(const std::string& path);

struct DirEntry
{
  std::string name;
  bool is_dir = false;
  bool is_game = false;
  bool is_parent = false;  // virtual "go to parent" row, always first
  int64_t size = 0;   // bytes, regular files only
  int64_t mtime = 0;  // seconds since epoch
};

enum class SortMode
{
  kName,
  kSize,
  kTime,
};

class DirBrowser
{
public:
  // Reads, filters and sorts the given directory.  Returns false (and fills
  // error) when the directory cannot be listed.
  bool Open(const std::string& path, std::string& error);

  // Move into the parent directory.
  bool GoParent();

  // Move the cursor by delta (-1 / +1); clamped to the list.
  void MoveCursor(int delta);

  // Handles the "confirm" action:
  //   * directory  -> enters it
  //   * game file  -> stores it in Selected() and returns true
  //   * anything else -> returns false without changing state
  bool Activate();

  void SetGamesOnly(bool enabled);
  bool GamesOnly() const { return games_only_; }
  void CycleSort();

  SortMode Sort() const { return sort_mode_; }

  const std::string& Directory() const { return directory_; }
  const std::vector<DirEntry>& Entries() const { return entries_; }
  size_t Cursor() const { return cursor_; }
  const std::string& Selected() const { return selected_; }
  bool CanGoParent() const { return can_go_parent_; }

private:
  void RefreshView();
  void ClampCursor();

  std::string directory_;
  std::vector<DirEntry> all_entries_;  // raw directory listing
  std::vector<DirEntry> entries_;      // filtered + sorted view
  size_t cursor_ = 0;
  std::string selected_;
  bool can_go_parent_ = false;
  bool games_only_ = false;
  SortMode sort_mode_ = SortMode::kName;
};
}
