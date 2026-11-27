// SPDX-License-Identifier: GPL-3.0-or-later
// document.* and edit.* (api/document.json).
#include "convert.h"
#include "engine.h"

#include "shapelogic/io/gzip_file.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace sl::engine {

namespace {

Reply documentNew(State& s, Params& p) {
  const GridKind grid = p.choice("grid", gridNames());
  const bool tilted = p.has("tray");
  const Json none;
  const Json& given = tilted ? p.value("tray") : none;
  SLE_CHECK(p.finish());
  const app::DocumentSettings settings{s.settings.undoDepth};
  if (tilted) {
    // a Tilted Square puzzle: a cube-grid puzzle with a tray (decision 0045)
    if (grid != GridKind::Bricks) {
      return coreError(Errc::InvalidArgument, "a tilted tray is for the bricks grid");
    }
    RpcResult<tilted::Tray> tray = readTraySettings(given, tilted::Tray{});
    if (!tray) {
      return std::unexpected(tray.error());
    }
    SLE_TRY(app::Document doc, app::Document::newTilted(std::move(*tray), settings));
    s.replaceDocument(std::move(doc), std::nullopt);
    return snapshot(s);
  }
  s.replaceDocument(app::Document(grid, settings), std::nullopt);
  return snapshot(s);
}

Reply documentOpen(State& s, Params& p) {
  const std::string path = p.string("path");
  // the file the document stands for: the one read, unless `as` names
  // another or (null) none, as a rescue copy reopens (cleanup item 10.11)
  std::optional<std::filesystem::path> file = std::filesystem::path(path);
  if (p.has("as")) {
    const Json& as = p.value("as");
    if (as.is_null()) {
      file = std::nullopt;
    } else if (as.is_string() && !as.get<std::string>().empty()) {
      file = std::filesystem::path(as.get<std::string>());
    } else {
      p.fail("'as' must be a file's path or null");
    }
  }
  const bool dirty = p.optBoolean("dirty", false);
  SLE_CHECK(p.finish());
  SLE_TRY(app::Document d,
          app::Document::open(path, app::DocumentSettings{s.settings.undoDepth}));
  if (dirty) {
    d.markUnsaved();
  }
  s.replaceDocument(std::move(d), std::move(file));
  return openedSnapshot(s);
}

Reply documentOpenBytes(State& s, Params& p) {
  const std::string data = p.string("data");
  const std::optional<std::string> path = p.optString("path");
  const bool dirty = p.optBoolean("dirty", false);
  SLE_CHECK(p.finish());
  const std::optional<std::string> bytes = base64Decode(data);
  if (!bytes) {
    return coreError(Errc::InvalidArgument, "'data' is not base64");
  }
  if (path && path->empty()) {
    return coreError(Errc::InvalidArgument, "'path' is empty");
  }
  SLE_TRY(app::Document d,
          app::Document::openBytes(*bytes, app::DocumentSettings{s.settings.undoDepth}));
  if (dirty) {
    d.markUnsaved();
  }
  s.replaceDocument(std::move(d),
                    path ? std::optional<std::filesystem::path>(*path) : std::nullopt);
  return openedSnapshot(s);
}

Reply documentImport(State& s, Params& p) {
  const app::ImportFormat format = p.choice("format", importFormatNames());
  const std::optional<std::string> path = p.optString("path");
  const std::optional<std::string> data = p.optString("data");
  SLE_CHECK(p.finish());
  if (path.has_value() == data.has_value()) {
    return coreError(Errc::InvalidArgument, "give the file's 'path' or its 'data', one of the two");
  }
  const app::DocumentSettings settings{s.settings.undoDepth};
  Result<app::Document> d = [&]() -> Result<app::Document> {
    if (path) {
      return app::Document::importFile(format, *path, settings);
    }
    const std::optional<std::string> bytes = base64Decode(*data);
    if (!bytes) {
      return fail(Errc::InvalidArgument, "'data' is not base64");
    }
    return app::Document::importBytes(format, *bytes, settings);
  }();
  SLE_CHECK(d);
  // no path: Save must not write over the other program's file
  s.replaceDocument(std::move(*d), std::nullopt);
  return openedSnapshot(s);
}

Reply documentExportPuzzlecad(State& s, Params& p) {
  const std::optional<app::Id> problem = p.optId("problem", IdKind::Problem);
  const std::string name = p.optString("name").value_or("");
  io::PuzzlecadOptions options;
  options.copies = p.optBoolean("copies", false);
  options.tray = p.optBoolean("tray", false);
  options.lip = static_cast<unsigned int>(p.optInteger("lip", 0, 0, kMaxLip));
  std::vector<app::Id> shapes;
  if (p.has("shapes")) {
    shapes = readShapeIds(p, p.value("shapes"));
    // (a list that is none failed first)
    if (problem) {
      p.fail("give 'problem' or 'shapes', not both");
    }
  }
  SLE_CHECK(p.finish());
  for (const app::Id id : shapes) {
    SLE_TRY(const unsigned int index, s.document().shapeIndex(id));
    options.shapes.push_back(index);
  }
  // the name goes into a comment line of the file
  SLE_CHECK(checkFileName(name));
  // the puzzle's file, or the export's when it has none (cb_Export_Scad)
  const std::string source = s.path ? s.path->string() : name;
  SLE_TRY(std::string text, s.document().exportPuzzlecad(problem, source, options));
  return Json{{"text", std::move(text)}};
}

Reply documentConvertGrid(State& s, Params& p) {
  const GridKind grid = p.choice("grid", gridNames());
  SLE_CHECK(p.finish());
  SLE_TRY(app::Document d, s.document().convertGrid(grid));
  // the original kept the file name (cb_Convert): Save writes there
  const std::optional<std::filesystem::path> path = s.path;
  s.replaceDocument(std::move(d), path);
  return snapshot(s);
}

Reply documentSave(State& s, Params& p) {
  const std::optional<std::string> given = p.optString("path");
  SLE_CHECK(p.finish());
  if (!given && !s.path) {
    return coreError(Errc::InvalidArgument, "the document has no file yet: give a path");
  }
  const std::filesystem::path path = given ? std::filesystem::path(*given) : *s.path;
  SLE_CHECK(s.document().save(path));
  s.path = path;
  return Json{{"revision", s.revision()}, {"path", path.string()}};
}

Reply documentSaveBytes(State& s, Params& p) {
  SLE_CHECK(p.finish());
  SLE_TRY(const std::string bytes, s.document().saveBytes());
  return Json{{"data", base64Encode(bytes)}};
}

Reply documentSaveRescue(State& s, Params& p) {
  const std::string path = p.string("path");
  const auto revision =
      static_cast<std::uint64_t>(p.integer("revision", 0, std::numeric_limits<std::int64_t>::max()));
  SLE_CHECK(p.finish());
  // the copy is of the document the caller saw: one opened since (a call
  // ahead of this one, or a new engine after a crash) is not its puzzle
  if (revision < s.documentSince() || revision > s.revision()) {
    return coreError(Errc::InvalidState, "the document is not the one of that revision");
  }
  SLE_TRY(const std::string bytes, s.document().saveBytes());
  // compressed (cleanup item 4.5: plain XML over about 48 MiB came back
  // from saveBytes in base64 longer than the 64 MiB line the engine reads),
  // at the fast level: the copy is written after every change, on the
  // thread that answers the edits, and level 9 took about as long again as
  // writing the XML
  SLE_CHECK(io::writeFileWhole(path, io::gzipCompress(bytes, io::kGzipFast)));
  return Json{{"revision", s.revision()}};
}

Reply documentGet(State& s, Params& p) {
  SLE_CHECK(p.finish());
  return snapshot(s);
}

Reply documentSetNotes(State& s, Params& p) {
  std::string text = p.string("text");
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().setNotes(text));
  return s.changed();
}

Reply editUndo(State& s, Params& p) {
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().undo());
  return s.changed();
}

Reply editRedo(State& s, Params& p) {
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().redo());
  return s.changed();
}

Reply editHistory(State& s, Params& p) {
  SLE_CHECK(p.finish());
  const app::HistoryInfo h = s.document().history();
  return Json{{"undo", h.undo}, {"redo", h.redo}};
}

Reply editBegin(State& s, Params& p) {
  std::string label = p.optString("label").value_or("Edit");
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().beginGroup(std::move(label)));
  return Json::object();
}

Reply editEnd(State& s, Params& p) {
  SLE_CHECK(p.finish());
  SLE_CHECK(s.document().endGroup());
  return s.changed();
}

}  // namespace

void addDocumentMethods(MethodTable& t) {
  t["document.new"] = documentNew;
  t["document.open"] = documentOpen;
  t["document.openBytes"] = documentOpenBytes;
  t["document.import"] = documentImport;
  t["document.exportPuzzlecad"] = documentExportPuzzlecad;
  t["document.convertGrid"] = documentConvertGrid;
  t["document.save"] = documentSave;
  t["document.saveBytes"] = documentSaveBytes;
  t["document.saveRescue"] = documentSaveRescue;
  t["document.get"] = documentGet;
  t["document.setNotes"] = documentSetNotes;
  t["edit.undo"] = editUndo;
  t["edit.redo"] = editRedo;
  t["edit.history"] = editHistory;
  t["edit.begin"] = editBegin;
  t["edit.end"] = editEnd;
}

}  // namespace sl::engine
