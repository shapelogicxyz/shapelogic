// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/lib/sliding.cpp's solver state, with the saved search
// that is new: the names of a problem's saved searches and their folders,
// state.bin, the log's texts of what is saved (cleanup item 21.1 moved them
// here from sliding_search.cpp).
#include "shapelogic/solve/slide_store.h"

#include "shapelogic/base/binary_io.h"
#include "shapelogic/base/fnv1a.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/gzip_file.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"
#include "shapelogic/model/slide_marks.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <istream>
#include <ostream>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

namespace sl::sliding::detail {

namespace {

// The magic of state.bin.
constexpr std::array<char, 8> kStateMagic = {'B', 'T', 'S', 'L', 'S', 'V', '0', '1'};
// A search from both ends with nested slides, which also names the goal
// end's visited sets. A name of its own, so that a build from before such
// searches takes the state for none of its own rather than carry the goal
// end on with the moves made forwards; every other search is saved as it
// always was, and read by any build.
constexpr std::array<char, 8> kStateMagicBothNested = {'B', 'T', 'S', 'L', 'S', 'V', '0', '2'};

using binary::get;
using binary::put;

void putString(std::ostream& out, const std::string& s) {
  put(out, static_cast<std::uint32_t>(s.size()));
  out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool getString(std::istream& in, std::string& s) {
  std::uint32_t n = 0;
  get(in, n);
  if (!in || n > 64) {
    return false;
  }
  s.resize(n);
  in.read(s.data(), n);
  return static_cast<bool>(in);
}

}  // namespace

void releaseFreeMemory() {
#if defined(__APPLE__)
  malloc_zone_pressure_relief(nullptr, 0);
#elif defined(__GLIBC__)
  malloc_trim(0);
#endif
}

std::uint64_t problemKeyOf(const Puzzle& puzzle, const Problem& problem) {
  Fnv1a f;
  f.u32(2U);  // the saved format
  if (problem.getUid() != 0) {
    f.u64(problem.getUid());
  }
  const Voxel& tray = problem.resultShape(puzzle);
  f.u32(tray.getX());
  f.u32(tray.getY());
  f.u32(tray.getZ());
  for (unsigned int i = 0; i < tray.getXYZ(); i++) {
    f.i32(tray.getState(i));
    f.u32(problem.slideMarks().start(i));
    f.u32(problem.slideMarks().goal(i));
  }
  for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
    const Voxel& shape = problem.partShape(puzzle, part);
    f.u32(shape.getX());
    f.u32(shape.getY());
    f.u32(shape.getZ());
    for (unsigned int i = 0; i < shape.getXYZ(); i++) {
      f.i32(shape.getState(i));
    }
    f.u32(problem.getPartMinimum(part));
    f.u32(problem.getPartMaximum(part));
  }
  return f.value();
}

std::string folderName(std::uint64_t key) {
  char name[40];
  std::snprintf(name, sizeof(name), "slide-%016llx", static_cast<unsigned long long>(key));
  return name;
}

std::string bytesText(std::uint64_t bytes) {
  char t[32];
  if (bytes >= 1000000000ULL) {
    std::snprintf(t, sizeof(t), "%.2f GB", static_cast<double>(bytes) / 1e9);
  } else {
    std::snprintf(t, sizeof(t), "%.1f MB", static_cast<double>(bytes) / 1e6);
  }
  return t;
}

std::string secondsText(std::chrono::steady_clock::duration d) {
  char t[32];
  std::snprintf(t, sizeof(t), "%.1f s", std::chrono::duration<double>(d).count());
  return t;
}

std::string savedText(const SavedSearch& st) {
  std::string t;
  for (int i = 0; i < 2; i++) {
    const SavedSide& s = st.side[i];
    if (i == 1 && !st.bidirectional) {
      t.append("; no goal end");
      continue;
    }
    t.append(i == 0 ? "the start end " : "; the goal end ")
        .append(std::to_string(s.depth))
        .append(" moves deep (newest level ")
        .append(s.cur.empty() ? "none" : s.cur)
        .append(", the one before ")
        .append(s.prev.empty() ? "none" : s.prev)
        .append("; kept levels");
    if (s.stored.empty()) {
      t.append(" none");
    }
    for (const auto& [level, name] : s.stored) {
      t.append(" ").append(std::to_string(level)).append("=").append(name);
    }
    t.append(")");
  }
  for (int i = 0; i < 2; i++) {
    const std::vector<std::string>& sets = i == 0 ? st.visitedSets : st.goalVisitedSets;
    if (i == 1 && sets.empty()) {
      continue;
    }
    t.append(i == 0 ? "; visited sets" : "; the goal end's visited sets");
    if (sets.empty()) {
      t.append(" none");
    }
    for (const std::string& name : sets) {
      t.append(" ").append(name);
    }
  }
  t.append("; ")
      .append(std::to_string(st.visited))
      .append(" arrangements visited, interval ")
      .append(std::to_string(st.interval))
      .append(", next file number ")
      .append(std::to_string(st.fileNumber));
  return t;
}

std::string folderText(const std::filesystem::path& dir) {
  std::error_code ec;
  std::uint64_t files = 0;
  std::uint64_t bytes = 0;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    files++;
    const std::uintmax_t n = e.file_size(ec);
    bytes += ec ? 0 : n;
  }
  return std::to_string(files) + (files == 1 ? " file, " : " files, ") + bytesText(bytes);
}

bool writeState(const std::filesystem::path& dir, const SavedSearch& st) {
  const Result<void> written = io::writeFileStreamed(dir / "state.bin", [&](std::ostream& out) {
    const bool bothNested = st.nested && st.bidirectional;
    out.write(bothNested ? kStateMagicBothNested.data() : kStateMagic.data(), kStateMagic.size());
    put(out, st.problemKey);
    put(out, st.searchKey);
    put(out, st.visited);
    put(out, st.oneWayRepeats);
    put(out, st.interval);
    put(out, st.fileNumber);
    put(out, static_cast<std::uint8_t>(st.nested ? 1 : 0));
    put(out, static_cast<std::uint8_t>(st.bidirectional ? 1 : 0));
    for (const SavedSide& s : st.side) {
      put(out, s.depth);
      putString(out, s.prev);
      putString(out, s.cur);
      put(out, static_cast<std::uint32_t>(s.stored.size()));
      for (const auto& [level, name] : s.stored) {
        put(out, level);
        putString(out, name);
      }
    }
    put(out, static_cast<std::uint32_t>(st.visitedSets.size()));
    for (const std::string& name : st.visitedSets) {
      putString(out, name);
    }
    if (bothNested) {
      put(out, static_cast<std::uint32_t>(st.goalVisitedSets.size()));
      for (const std::string& name : st.goalVisitedSets) {
        putString(out, name);
      }
    }
    return static_cast<bool>(out);
  });
  return written.has_value();
}

bool readState(const std::filesystem::path& dir, SavedSearch& st, bool whole) {
  std::ifstream in(dir / "state.bin", std::ios::binary);
  std::array<char, kStateMagic.size()> magic{};
  in.read(magic.data(), magic.size());
  const bool bothNested = magic == kStateMagicBothNested;
  if (!in || (magic != kStateMagic && !bothNested)) {
    return false;
  }
  get(in, st.problemKey);
  get(in, st.searchKey);
  if (!whole) {
    return static_cast<bool>(in);
  }
  get(in, st.visited);
  get(in, st.oneWayRepeats);
  get(in, st.interval);
  get(in, st.fileNumber);
  std::uint8_t nested = 0;
  std::uint8_t bidirectional = 0;
  get(in, nested);
  get(in, bidirectional);
  st.nested = nested != 0;
  st.bidirectional = bidirectional != 0;
  for (SavedSide& s : st.side) {
    get(in, s.depth);
    std::uint32_t n = 0;
    if (!getString(in, s.prev) || !getString(in, s.cur)) {
      return false;
    }
    get(in, n);
    if (!in || n > 1000000) {
      return false;
    }
    s.stored.resize(n);
    for (auto& [level, name] : s.stored) {
      get(in, level);
      if (!getString(in, name)) {
        return false;
      }
    }
  }
  for (std::vector<std::string>* sets : {&st.visitedSets, &st.goalVisitedSets}) {
    if (sets == &st.goalVisitedSets && !bothNested) {
      break;
    }
    std::uint32_t n = 0;
    get(in, n);
    if (!in || n > 100000) {
      return false;
    }
    sets->resize(n);
    for (std::string& name : *sets) {
      if (!getString(in, name)) {
        return false;
      }
    }
  }
  return static_cast<bool>(in) && st.interval != 0 && bothNested == (st.nested && st.bidirectional);
}

}  // namespace sl::sliding::detail
