// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from mainWindow_c::cb_ExportPaused and cb_ImportPaused
// (src/gui/mainwindow_file.cpp): SOLVE_MAGIC, writeSection,
// writePackedSection, readSectionHeader (a name of at most 4096 bytes),
// isPlainFileName and the export and import loops. The original went
// through a temporary file for the gzip-compressed puzzle; this compresses
// in memory, as writeGzipFile does.
#include "shapelogic/io/paused_solve.h"

#include "shapelogic/io/blockpack.h"
#include "shapelogic/io/gzip_file.h"
#include "shapelogic/io/puzzle_file.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>
#include <vector>

namespace sl::io {

namespace {

constexpr std::string_view kMagic = "BTSOLVE2";

template <class T>
void putLe(std::string& out, T v) {
  for (std::size_t i = 0; i < sizeof(T); i++) {
    out += static_cast<char>((v >> (8 * i)) & 0xFF);
  }
}

void section(std::string& out, std::string_view name, std::string_view data) {
  putLe<std::uint32_t>(out, static_cast<std::uint32_t>(name.size()));
  out += name;
  putLe<std::uint64_t>(out, data.size());
  out += data;
}

Result<PausedSolve> incomplete() { return fail(Errc::ParseError, "the file could not be read completely"); }

// The most bytes a "puzzle" or "problem" section may claim: more than any
// puzzle file holds (a file could be crafted to claim more).
constexpr std::uint64_t kMaxPuzzleSection = kMaxInflatedBytes;

void putName(std::ostream& out, std::string_view name) {
  std::string head;
  putLe<std::uint32_t>(head, static_cast<std::uint32_t>(name.size()));
  head += name;
  out.write(head.data(), static_cast<std::streamsize>(head.size()));
}

void putSize(std::ostream& out, std::uint64_t size) {
  std::string bytes;
  putLe<std::uint64_t>(bytes, size);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// A file packed by blockpack: its size is known only once it is written,
// so the size field is filled in afterwards (writePackedSection).
bool writePackedSection(std::ostream& out, std::string_view name, const std::filesystem::path& file,
                        bool keys, unsigned int threads) {
  putName(out, name);
  const std::streampos sizeAt = out.tellp();
  putSize(out, 0);
  const std::uint64_t size = blockpack::pack(file, out, keys, threads);
  if (size == 0 || !out) {
    return false;
  }
  const std::streampos end = out.tellp();
  out.seekp(sizeAt);
  putSize(out, size);
  out.seekp(end);
  return static_cast<bool>(out);
}

// A bare file name, such as "state.bin": nothing that could lead out of the
// folder it is written into (a file could be crafted to try).
bool isPlainFileName(std::string_view n) {
  return !n.empty() && n != "." && n != ".." && n.find_first_of("/\\:") == std::string_view::npos;
}

template <class T>
bool readLe(std::istream& in, T& v) {
  std::array<unsigned char, sizeof(T)> b{};
  if (!in.read(reinterpret_cast<char*>(b.data()), sizeof(T))) {
    return false;
  }
  v = 0;
  for (std::size_t i = 0; i < sizeof(T); i++) {
    v = static_cast<T>(v | (static_cast<T>(b[i]) << (8 * i)));
  }
  return true;
}

// What stands where the next section's header should: one, the end of the
// file, or neither (a header cut short, or a name longer than any section
// has: 4096 bytes, the original's bound).
enum class Header : std::uint8_t { Section, End, Bad };

// The next section's name and size.
Header readSectionHeader(std::istream& in, std::string& name, std::uint64_t& size) {
  if (in.peek() == std::istream::traits_type::eof()) {
    return Header::End;
  }
  std::uint32_t len = 0;
  if (!readLe(in, len) || len > 4096) {
    return Header::Bad;
  }
  name.assign(len, '\0');
  return in.read(name.data(), len) && readLe(in, size) ? Header::Section : Header::Bad;
}

}  // namespace

std::string writePausedSolve(const Puzzle& puzzle, unsigned int problem) {
  std::string out(kMagic);
  section(out, "puzzle", gzipCompress(writePuzzle(puzzle)));
  section(out, "problem", std::to_string(problem));
  return out;
}

std::uint64_t savedLevelBytes(const std::filesystem::path& folder) {
  std::uint64_t bytes = 0;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
    if (e.path().extension() == ".lvl") {
      bytes += e.file_size(ec);
    }
  }
  return bytes;
}

Result<void> savePausedSolve(const Puzzle& puzzle, unsigned int problem,
                             const std::filesystem::path& path, const SavedSearchFiles* search,
                             unsigned int threads) {
  // beside the file, renamed over it once whole (as a puzzle is saved)
  if (search == nullptr) {
    return writeFileWhole(path, writePausedSolve(puzzle, problem));
  }
  // the saved search: its state, and the levels if wanted (in name order;
  // the original took the folder's order)
  std::vector<std::filesystem::path> files;
  std::vector<std::filesystem::path> levels;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(search->folder, ec)) {
    if (e.path().filename() == "state.bin") {
      files.push_back(e.path());
    } else if (e.path().extension() == ".lvl") {
      levels.push_back(e.path());
    }
  }
  std::sort(levels.begin(), levels.end());
  if (search->withLevels) {
    files.insert(files.end(), levels.begin(), levels.end());
  }
  const std::string head = writePausedSolve(puzzle, problem);
  return writeFileStreamed(path, [&](std::ostream& out) {
    out.write(head.data(), static_cast<std::streamsize>(head.size()));
    for (const std::filesystem::path& file : files) {
      if (!writePackedSection(out, "searchz/" + file.filename().string(), file,
                              file.extension() == ".lvl", threads)) {
        return false;
      }
    }
    return static_cast<bool>(out);
  });
}

Result<PausedSolve> loadPausedSolve(const std::filesystem::path& path,
                                    const SavedSearchFolder& searchFolder, unsigned int threads) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return fail(Errc::IoError, "could not open " + path.string());
  }
  Result<PausedSolve> read = readPausedSolve(in, searchFolder, threads);
  if (!read && in.bad()) {
    return fail(Errc::IoError, "could not read " + path.string());
  }
  return read;
}

Result<PausedSolve> readPausedSolve(std::istream& in, const SavedSearchFolder& searchFolder,
                                    unsigned int threads) {
  // how long the file is: a section is checked against what is left of it
  // before room is made for it or it is passed over (a file can claim any
  // size)
  in.seekg(0, std::ios::end);
  const std::streamoff total = in.tellg();
  in.seekg(0, std::ios::beg);
  if (!in || total < 0) {
    return incomplete();
  }
  const auto left = [&in, total]() -> std::uint64_t {
    const std::streamoff at = in.tellg();
    return at < 0 || at > total ? 0 : static_cast<std::uint64_t>(total - at);
  };
  std::string magic(kMagic.size(), '\0');
  if (!in.read(magic.data(), static_cast<std::streamsize>(magic.size())) || magic != kMagic) {
    return fail(Errc::ParseError, "not a paused solver state exported by ShapeLogic");
  }
  // read section by section: a saved search's levels can be gigabytes
  std::optional<Puzzle> puzzle;
  std::optional<unsigned int> problem;
  std::filesystem::path searchDir;
  // The saved search is unpacked beside its folder and takes its place
  // only once the whole file is read: an import that fails part of the
  // way leaves a saved search that was there as it was (decision 0041).
  std::filesystem::path unpackDir;
  struct Unpacked {
    std::filesystem::path* dir;
    Unpacked(const Unpacked&) = delete;
    Unpacked& operator=(const Unpacked&) = delete;
    explicit Unpacked(std::filesystem::path* d) : dir(d) {}
    ~Unpacked() {
      if (!dir->empty()) {
        std::error_code ec;
        std::filesystem::remove_all(*dir, ec);
      }
    }
  };
  const Unpacked unpacked(&unpackDir);
  std::string name;
  std::uint64_t size = 0;
  while (true) {
    const Header header = readSectionHeader(in, name, size);
    if (header == Header::End) {
      break;
    }
    // a section that ends past the file: the file is cut short
    if (header == Header::Bad || size > left()) {
      return incomplete();
    }
    if (name == "puzzle" || name == "problem") {
      if (size > kMaxPuzzleSection) {
        return incomplete();
      }
      std::string data(static_cast<std::size_t>(size), '\0');
      if (!in.read(data.data(), static_cast<std::streamsize>(size))) {
        return incomplete();
      }
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
        continue;
      }
      // the original read it with strtoul
      unsigned int n = 0;
      (void)std::from_chars(data.data(), data.data() + data.size(), n);
      problem = n;
      // where a stacking problem's saved search goes, and where it is
      // unpacked first
      if (searchFolder && puzzle && puzzle->getGridType().getType() == GridKind::Stacking &&
          n < puzzle->getNumberOfProblems()) {
        searchDir = searchFolder(*puzzle, n);
        if (searchDir.empty()) {
          return incomplete();
        }
        std::error_code ec;
        if (!unpackDir.empty()) {
          std::filesystem::remove_all(unpackDir, ec);
        }
        unpackDir = searchDir;
        unpackDir += ".import";
        std::filesystem::remove_all(unpackDir, ec);
        std::filesystem::create_directories(unpackDir, ec);
      }
    } else if (name.starts_with("searchz/") && !unpackDir.empty() &&
               isPlainFileName(std::string_view(name).substr(8))) {
      if (!blockpack::unpack(in, size, unpackDir / name.substr(8), threads)) {
        return incomplete();
      }
    } else {
      // anything else is skipped
      in.seekg(static_cast<std::streamoff>(size), std::ios::cur);
      if (!in) {
        return incomplete();
      }
    }
  }
  if (in.bad() || !puzzle || !problem || *problem >= puzzle->getNumberOfProblems()) {
    return incomplete();
  }
  if (!unpackDir.empty()) {
    // the file is whole: what it held takes the place of what was there
    // (nothing, when the file held no saved search)
    std::error_code ec;
    std::filesystem::remove_all(searchDir, ec);
    std::filesystem::rename(unpackDir, searchDir, ec);
    if (ec) {
      return fail(Errc::IoError, "could not put the saved search in " + searchDir.string());
    }
    unpackDir.clear();
  }
  return PausedSolve{std::move(*puzzle), *problem};
}

}  // namespace sl::io
