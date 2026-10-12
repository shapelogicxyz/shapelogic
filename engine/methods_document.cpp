// SPDX-License-Identifier: GPL-3.0-or-later
// document.* and edit.* (api/document.json).
#include "convert.h"
#include "engine.h"

namespace bt::engine {

namespace {

Reply documentNew(State& s, Params& p) {
  const GridKind grid = p.choice("grid", gridNames());
  BTE_CHECK(p.finish());
  s.replaceDocument(app::Document(grid, app::DocumentSettings{s.settings.undoDepth}), std::nullopt);
  return snapshot(s);
}

Reply documentOpen(State& s, Params& p) {
  const std::string path = p.string("path");
  BTE_CHECK(p.finish());
  BTE_TRY(app::Document d,
          app::Document::open(path, app::DocumentSettings{s.settings.undoDepth}));
  s.replaceDocument(std::move(d), std::filesystem::path(path));
  return openedSnapshot(s);
}

Reply documentOpenBytes(State& s, Params& p) {
  const std::string data = p.string("data");
  const std::optional<std::string> path = p.optString("path");
  const bool dirty = p.optBoolean("dirty", false);
  BTE_CHECK(p.finish());
  const std::optional<std::string> bytes = base64Decode(data);
  if (!bytes) {
    return coreError(Errc::InvalidArgument, "'data' is not base64");
  }
  if (path && path->empty()) {
    return coreError(Errc::InvalidArgument, "'path' is empty");
  }
  BTE_TRY(app::Document d,
          app::Document::openBytes(*bytes, app::DocumentSettings{s.settings.undoDepth}));
  if (dirty) {
    d.markUnsaved();
  }
  s.replaceDocument(std::move(d),
                    path ? std::optional<std::filesystem::path>(*path) : std::nullopt);
  return openedSnapshot(s);
}

Reply documentImport(State& s, Params& p) {
  static const Names<app::ImportFormat> kFormats = {
      {"puzzleSolver3D", app::ImportFormat::PuzzleSolver3D},
      {"puzzlecad", app::ImportFormat::Puzzlecad}};
  const app::ImportFormat format = p.choice("format", kFormats);
  const std::optional<std::string> path = p.optString("path");
  const std::optional<std::string> data = p.optString("data");
  BTE_CHECK(p.finish());
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
  BTE_CHECK(d);
  // no path: Save must not write over the other program's file
  s.replaceDocument(std::move(*d), std::nullopt);
  return openedSnapshot(s);
}

Reply documentExportPuzzlecad(State& s, Params& p) {
  const std::optional<app::Id> problem = p.optId("problem", IdKind::Problem);
  const std::string name = p.optString("name").value_or("");
  BTE_CHECK(p.finish());
  // the name goes into a comment line of the file
  if (name.size() > 255) {
    return coreError(Errc::InvalidArgument, "the name is longer than 255 bytes");
  }
  for (const char c : name) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
      return coreError(Errc::InvalidArgument, "the name cannot hold control characters");
    }
  }
  // the puzzle's file, or the export's when it has none (cb_Export_Scad)
  const std::string source = s.path ? s.path->string() : name;
  BTE_TRY(std::string text, s.document().exportPuzzlecad(problem, source));
  return Json{{"text", std::move(text)}};
}

Reply documentSave(State& s, Params& p) {
  const std::optional<std::string> given = p.optString("path");
  BTE_CHECK(p.finish());
  if (!given && !s.path) {
    return coreError(Errc::InvalidArgument, "the document has no file yet: give a path");
  }
  const std::filesystem::path path = given ? std::filesystem::path(*given) : *s.path;
  BTE_CHECK(s.document().save(path));
  s.path = path;
  return Json{{"revision", s.revision()}, {"path", path.string()}};
}

Reply documentSaveBytes(State& s, Params& p) {
  BTE_CHECK(p.finish());
  BTE_TRY(const std::string bytes, s.document().saveBytes());
  return Json{{"data", base64Encode(bytes)}};
}

Reply documentGet(State& s, Params& p) {
  BTE_CHECK(p.finish());
  return snapshot(s);
}

Reply documentSetNotes(State& s, Params& p) {
  std::string text = p.string("text");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setNotes(std::move(text)));
  return s.changed();
}

Reply editUndo(State& s, Params& p) {
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().undo());
  return s.changed();
}

Reply editRedo(State& s, Params& p) {
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().redo());
  return s.changed();
}

Reply editHistory(State& s, Params& p) {
  BTE_CHECK(p.finish());
  const app::HistoryInfo h = s.document().history();
  return Json{{"undo", h.undo}, {"redo", h.redo}};
}

Reply editBegin(State& s, Params& p) {
  std::string label = p.optString("label").value_or("Edit");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().beginGroup(std::move(label)));
  return Json::object();
}

Reply editEnd(State& s, Params& p) {
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().endGroup());
  return s.changed();
}

}  // namespace

void addDocumentMethods(MethodTable& t) {
  t["document.new"] = documentNew;
  t["document.open"] = documentOpen;
  t["document.openBytes"] = documentOpenBytes;
  t["document.import"] = documentImport;
  t["document.exportPuzzlecad"] = documentExportPuzzlecad;
  t["document.save"] = documentSave;
  t["document.saveBytes"] = documentSaveBytes;
  t["document.get"] = documentGet;
  t["document.setNotes"] = documentSetNotes;
  t["edit.undo"] = editUndo;
  t["edit.redo"] = editRedo;
  t["edit.history"] = editHistory;
  t["edit.begin"] = editBegin;
  t["edit.end"] = editEnd;
}

}  // namespace bt::engine
