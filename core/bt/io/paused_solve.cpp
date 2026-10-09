// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from mainWindow_c::cb_ExportPaused and cb_ImportPaused
// (src/gui/mainwindow_file.cpp): SOLVE_MAGIC, writeSection,
// readSectionHeader (a name of at most 4096 bytes) and the import loop.
// The original went through a temporary file for the gzip-compressed
// puzzle; this compresses in memory, as writeGzipFile does.
#include "bt/io/paused_solve.h"

#include "bt/io/gzip_file.h"
#include "bt/io/puzzle_file.h"

#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>

namespace bt::io {

namespace {

constexpr std::string_view kMagic = "BTSOLVE2";

template <class T>
void putLe(std::string& out, T v) {
  for (std::size_t i = 0; i < sizeof(T); i++) {
    out += static_cast<char>((v >> (8 * i)) & 0xFF);
  }
}

template <class T>
std::optional<T> getLe(std::string_view& in) {
  if (in.size() < sizeof(T)) {
    return std::nullopt;
  }
  T v = 0;
  for (std::size_t i = 0; i < sizeof(T); i++) {
    v = static_cast<T>(v | (static_cast<T>(static_cast<unsigned char>(in[i])) << (8 * i)));
  }
  in.remove_prefix(sizeof(T));
  return v;
}

void section(std::string& out, std::string_view name, std::string_view data) {
  putLe<std::uint32_t>(out, static_cast<std::uint32_t>(name.size()));
  out += name;
  putLe<std::uint64_t>(out, data.size());
  out += data;
}

Result<PausedSolve> incomplete() { return fail(Errc::ParseError, "the file could not be read completely"); }

}  // namespace

std::string writePausedSolve(const Puzzle& puzzle, unsigned int problem) {
  std::string out(kMagic);
  section(out, "puzzle", gzipCompress(writePuzzle(puzzle)));
  section(out, "problem", std::to_string(problem));
  return out;
}

Result<void> savePausedSolve(const Puzzle& puzzle, unsigned int problem,
                             const std::filesystem::path& path) {
  const std::string bytes = writePausedSolve(puzzle, problem);
  // beside the file, renamed over it once whole (as a puzzle is saved)
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  if (!out) {
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return fail(Errc::IoError, "could not write " + path.string());
  }
  return replaceWith(tmp, path);
}

Result<PausedSolve> parsePausedSolve(std::string_view in) {
  if (!in.starts_with(kMagic)) {
    return fail(Errc::ParseError, "not a paused solver state exported by BurrTools");
  }
  in.remove_prefix(kMagic.size());
  std::optional<Puzzle> puzzle;
  std::optional<unsigned int> problem;
  while (!in.empty()) {
    const std::optional<std::uint32_t> len = getLe<std::uint32_t>(in);
    if (!len || *len > 4096 || in.size() < *len) {
      return incomplete();
    }
    const std::string_view name = in.substr(0, *len);
    in.remove_prefix(*len);
    const std::optional<std::uint64_t> size = getLe<std::uint64_t>(in);
    if (!size || in.size() < *size) {
      return incomplete();
    }
    const std::string_view data = in.substr(0, static_cast<std::size_t>(*size));
    in.remove_prefix(static_cast<std::size_t>(*size));
    if (name == "puzzle") {
      Result<std::string> xml = gunzipIfCompressed(data);
      if (!xml) {
        return std::unexpected(xml.error());
      }
      Result<Puzzle> p = parsePuzzle(*xml);
      if (!p) {
        return fail(Errc::ParseError, "the puzzle could not be read: " + p.error().message);
      }
      puzzle = std::move(*p);
    } else if (name == "problem") {
      // the original read it with strtoul
      unsigned int n = 0;
      (void)std::from_chars(data.data(), data.data() + data.size(), n);
      problem = n;
    }
    // anything else (a stacking solve's saved search) is skipped
  }
  if (!puzzle || !problem || *problem >= puzzle->getNumberOfProblems()) {
    return incomplete();
  }
  return PausedSolve{std::move(*puzzle), *problem};
}

Result<PausedSolve> loadPausedSolve(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return fail(Errc::IoError, "could not open " + path.string());
  }
  const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (f.bad()) {
    return fail(Errc::IoError, "could not read " + path.string());
  }
  return parsePausedSolve(bytes);
}

}  // namespace bt::io
