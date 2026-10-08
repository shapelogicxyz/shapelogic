// SPDX-License-Identifier: GPL-3.0-or-later
// problem.* (api/problem.json).
#include "convert.h"
#include "engine.h"

#include "bt/app/queries.h"

namespace bt::engine {

namespace {

constexpr std::int64_t kMaxPieces = 0xFFFF;

Json made(State& s, app::Id problem) {
  return Json{{"problem", formatId(IdKind::Problem, problem)}, {"revision", s.revision()}};
}

Reply problemList(State& s, Params& p) {
  BTE_CHECK(p.finish());
  Json problems = Json::array();
  for (unsigned int i = 0; i < s.document().puzzle().getNumberOfProblems(); i++) {
    problems.push_back(problemSummary(s, i));
  }
  return Json{{"problems", std::move(problems)}};
}

Reply problemGet(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_TRY(const unsigned int index, s.document().problemIndex(problem));
  return problemDetail(s, index);
}

Reply problemCreate(State& s, Params& p) {
  BTE_CHECK(p.finish());
  BTE_TRY(const app::Id id, s.document().createProblem());
  return made(s, id);
}

Reply problemDuplicate(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_TRY(const app::Id id, s.document().duplicateProblem(problem));
  return made(s, id);
}

Reply problemDelete(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().deleteProblem(problem));
  return s.changed();
}

Reply problemMove(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const unsigned int to = p.count("toIndex");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().moveProblem(problem, to));
  return s.changed();
}

Reply problemRename(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  std::string name = p.string("name");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().renameProblem(problem, std::move(name)));
  return s.changed();
}

Reply problemSetResult(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id shape = p.id("shape", IdKind::Shape);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setResult(problem, shape));
  return s.changed();
}

Reply problemSetPieceCount(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id shape = p.id("shape", IdKind::Shape);
  const auto min = static_cast<unsigned int>(p.integer("min", 0, kMaxPieces));
  const auto max = static_cast<unsigned int>(p.integer("max", 0, kMaxPieces));
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setPieceCount(problem, shape, min, max));
  return s.changed();
}

Reply problemAddPiece(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id shape = p.id("shape", IdKind::Shape);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().addPiece(problem, shape));
  return s.changed();
}

Reply problemRemovePiece(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id shape = p.id("shape", IdKind::Shape);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().removePiece(problem, shape));
  return s.changed();
}

Reply problemSetRangeAll(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const auto min = static_cast<unsigned int>(p.integer("min", 0, kMaxPieces));
  const auto max = static_cast<unsigned int>(p.integer("max", 0, kMaxPieces));
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setRangeAll(problem, min, max));
  return s.changed();
}

Reply problemAddOneOfEach(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().addOneOfEach(problem));
  return s.changed();
}

Reply problemRemoveAllPieces(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().removeAllPieces(problem));
  return s.changed();
}

Reply problemMovePart(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id shape = p.id("shape", IdKind::Shape);
  const auto delta = static_cast<int>(p.integer("delta", -1, 1));
  if (delta == 0) {
    p.fail("'delta' must be -1 or 1");
  }
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().movePart(problem, shape, delta));
  return s.changed();
}

Reply problemSetGroup(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id shape = p.id("shape", IdKind::Shape);
  const auto group = static_cast<unsigned int>(p.integer("group", 0, kMaxPieces));
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setGroup(problem, shape, group));
  return s.changed();
}

Reply problemSetGroupCount(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id shape = p.id("shape", IdKind::Shape);
  const auto group = static_cast<unsigned int>(p.integer("group", 1, kMaxPieces));
  const auto count = static_cast<unsigned int>(p.integer("count", 0, kMaxPieces));
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setGroupCount(problem, shape, group, count));
  return s.changed();
}

Reply problemDeleteGroup(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const auto group = static_cast<unsigned int>(p.integer("group", 1, kMaxPieces));
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().deleteGroup(problem, group));
  return s.changed();
}

Reply problemSetColourRule(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id piece = p.id("pieceColour", IdKind::Colour);
  const app::Id result = p.id("resultColour", IdKind::Colour);
  const bool allowed = p.boolean("allowed");
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setColourRule(problem, piece, result, allowed));
  return s.changed();
}

Reply problemSetColourRules(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::ColourPreset preset =
      p.choice("preset", Names<app::ColourPreset>{{"eachOnItself", app::ColourPreset::EachOnItself},
                                                  {"allowAll", app::ColourPreset::AllowAll},
                                                  {"clear", app::ColourPreset::Clear}});
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setColourRules(problem, preset));
  return s.changed();
}

Reply problemFit(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_TRY(const app::FitStats f, s.document().fit(problem));
  return fitJson(f);
}

Reply problemKind(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().problemIndex(problem));
  const app::ProblemKind kind = app::problemKind(s.document().puzzle());
  const std::string_view name = kind == app::ProblemKind::Sliding    ? "sliding"
                                : kind == app::ProblemKind::Stacking ? "stacking"
                                                                     : "brick";
  return Json{{"kind", name}, {"supported", kind == app::ProblemKind::Brick}};
}

Reply problemSetSolverSettings(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const Json& settings = p.value("settings");
  std::string error;
  std::optional<std::map<std::string, std::string>> o;
  if (p.has("settings")) {
    o = solverSettingsFrom(settings, error);
    if (!o) {
      p.fail(error);
    }
  }
  BTE_CHECK(p.finish());
  BTE_CHECK(s.document().setSolverOptions(problem, std::move(*o)));
  return s.changed();
}

}  // namespace

void addProblemMethods(MethodTable& t) {
  t["problem.list"] = problemList;
  t["problem.get"] = problemGet;
  t["problem.create"] = problemCreate;
  t["problem.duplicate"] = problemDuplicate;
  t["problem.delete"] = problemDelete;
  t["problem.move"] = problemMove;
  t["problem.rename"] = problemRename;
  t["problem.setResult"] = problemSetResult;
  t["problem.setPieceCount"] = problemSetPieceCount;
  t["problem.addPiece"] = problemAddPiece;
  t["problem.removePiece"] = problemRemovePiece;
  t["problem.setRangeAll"] = problemSetRangeAll;
  t["problem.addOneOfEach"] = problemAddOneOfEach;
  t["problem.removeAllPieces"] = problemRemoveAllPieces;
  t["problem.movePart"] = problemMovePart;
  t["problem.setGroup"] = problemSetGroup;
  t["problem.setGroupCount"] = problemSetGroupCount;
  t["problem.deleteGroup"] = problemDeleteGroup;
  t["problem.setColourRule"] = problemSetColourRule;
  t["problem.setColourRules"] = problemSetColourRules;
  t["problem.fit"] = problemFit;
  t["problem.kind"] = problemKind;
  t["problem.setSolverSettings"] = problemSetSolverSettings;
}

}  // namespace bt::engine
