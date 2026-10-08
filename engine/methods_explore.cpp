// SPDX-License-Identifier: GPL-3.0-or-later
// explore.* (api/explore.json): the explorers live in the engine behind
// handles, each on a copy of the puzzle.
#include "convert.h"
#include "engine.h"

namespace bt::engine {

namespace {

RpcResult<app::AssemblyExplorer*> assemblyExplorer(State& s, std::uint64_t handle) {
  const auto it = s.assemblyExplorers.find(handle);
  if (it == s.assemblyExplorers.end()) {
    return std::unexpected(toRpc(Error{Errc::NotFound, "there is no such explorer"}));
  }
  return &it->second;
}

RpcResult<app::MovementExplorer*> movementExplorer(State& s, std::uint64_t handle) {
  const auto it = s.movementExplorers.find(handle);
  if (it == s.movementExplorers.end()) {
    return std::unexpected(toRpc(Error{Errc::NotFound, "there is no such explorer"}));
  }
  return &it->second;
}

Json nodeJson(const app::MovementExplorer::Node& n) {
  Json pieces = Json::array();
  for (std::size_t i = 0; i < n.pieces.size(); i++) {
    pieces.push_back(Json{{"piece", n.pieces[i]}, {"at", Json::array({n.x[i], n.y[i], n.z[i]})}});
  }
  return Json{{"node", n.id},
              {"parent", n.parent},
              {"label", n.label},
              {"pieces", std::move(pieces)},
              {"children", n.children}};
}

RpcResult<const app::MovementExplorer::Node*> node(const app::MovementExplorer& m, unsigned int id) {
  if (!m.exists(id)) {
    return std::unexpected(toRpc(Error{Errc::NotFound, "there is no such position"}));
  }
  return &m.node(id);
}

Reply exploreOpenAssembly(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const SolverType solver = p.optChoice("solver", solverNames()).value_or(s.settings.solver);
  MatrixOptions m;
  m.keepMirrors = p.optBoolean("keepMirrors", false);
  m.keepRotations = p.optBoolean("keepRotations", false);
  m.completeRotations = p.optBoolean("completeRotations", false);
  m.strictColors = p.optBoolean("strictColours", false);
  const bool reduce = p.optBoolean("reduce", true);
  BTE_CHECK(p.finish());
  BTE_TRY(app::AssemblyExplorer e, s.document().exploreAssembly(problem, solver, m, reduce));
  const std::uint64_t handle = s.newHandle();
  Json j = Json{{"handle", formatId(IdKind::AssemblyExplorer, handle)},
                {"pieces", e.pieceCount()},
                {"placementsSupported", e.placementsSupported()},
                {"finished", e.finished()}};
  s.assemblyExplorers.emplace(handle, std::move(e));
  return j;
}

Reply explorePlacements(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::AssemblyExplorer);
  const unsigned int piece = p.count("piece");
  BTE_CHECK(p.finish());
  BTE_TRY(app::AssemblyExplorer* e, assemblyExplorer(s, handle));
  if (!e->placementsSupported()) {
    return coreError(Errc::Unsupported, "this assembler has no placement browser");
  }
  if (piece >= e->pieceCount()) {
    return coreError(Errc::InvalidArgument, "there is no such piece");
  }
  Json out = Json::array();
  for (const app::PiecePlacement& pl : e->placements(piece)) {
    out.push_back(Json{{"transformation", pl.transformation}, {"at", Json::array({pl.x, pl.y, pl.z})}});
  }
  return Json{{"placements", std::move(out)}};
}

Reply exploreStep(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::AssemblyExplorer);
  // capped: requests are served one at a time, and a long search would hold up the rest
  const auto steps = static_cast<unsigned long>(p.optInteger("steps", 1, 1, 1000000));
  BTE_CHECK(p.finish());
  BTE_TRY(app::AssemblyExplorer* e, assemblyExplorer(s, handle));
  e->step(steps);
  return Json{{"finished", e->finished()}, {"placements", placementsJson(e->currentAssembly())}};
}

Reply exploreOpenMovements(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id solution = p.id("solution", IdKind::Solution);
  const bool rotations = p.optBoolean("rotations", false);
  BTE_CHECK(p.finish());
  BTE_TRY(app::MovementExplorer m, s.document().exploreMovements(problem, solution, rotations));
  const std::uint64_t handle = s.newHandle();
  Json j = Json{{"handle", formatId(IdKind::MovementExplorer, handle)},
                {"root", nodeJson(m.root())},
                {"directions", m.directionLabels()}};
  s.movementExplorers.emplace(handle, std::move(m));
  return j;
}

Reply exploreNode(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::MovementExplorer);
  const unsigned int id = p.count("node");
  BTE_CHECK(p.finish());
  BTE_TRY(app::MovementExplorer* m, movementExplorer(s, handle));
  BTE_TRY(const app::MovementExplorer::Node* n, node(*m, id));
  return nodeJson(*n);
}

Reply exploreAnalyse(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::MovementExplorer);
  const unsigned int id = p.count("node");
  BTE_CHECK(p.finish());
  BTE_TRY(app::MovementExplorer* m, movementExplorer(s, handle));
  BTE_CHECK(node(*m, id));
  BTE_TRY(const std::vector<unsigned int> added, m->analyse(id));
  Json nodes = Json::array();
  for (const unsigned int n : added) {
    nodes.push_back(nodeJson(m->node(n)));
  }
  return Json{{"nodes", std::move(nodes)}};
}

Reply exploreAddMove(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::MovementExplorer);
  const unsigned int id = p.count("node");
  const Json& list = p.value("pieces");
  std::vector<unsigned int> pieces;
  if (!list.is_array() || list.empty()) {
    p.fail("'pieces' must be a list of piece numbers");
  } else {
    for (const Json& v : list) {
      if (!v.is_number_unsigned() || v.get<std::uint64_t>() > 0xFFFF) {
        p.fail("'pieces' must be a list of piece numbers");
        break;
      }
      pieces.push_back(v.get<unsigned int>());
    }
  }
  const unsigned int direction = p.count("direction", 0xFFFF);
  const bool reverse = p.optBoolean("reverse", false);
  BTE_CHECK(p.finish());
  BTE_TRY(app::MovementExplorer* m, movementExplorer(s, handle));
  BTE_CHECK(node(*m, id));
  BTE_TRY(const unsigned int n, m->addMove(id, pieces, direction, reverse));
  return nodeJson(m->node(n));
}

Reply exploreFindMatching(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::MovementExplorer);
  const unsigned int id = p.count("node");
  const unsigned int piece = p.count("piece", 0xFFFF);
  const std::array<int, 3> delta = p.cell("delta");
  BTE_CHECK(p.finish());
  BTE_TRY(app::MovementExplorer* m, movementExplorer(s, handle));
  BTE_CHECK(node(*m, id));
  BTE_TRY(const unsigned int n, m->findMatching(id, piece, delta[0], delta[1], delta[2]));
  return nodeJson(m->node(n));
}

Reply explorePrune(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::MovementExplorer);
  const unsigned int id = p.count("node");
  BTE_CHECK(p.finish());
  BTE_TRY(app::MovementExplorer* m, movementExplorer(s, handle));
  BTE_CHECK(node(*m, id));
  m->prune(id);
  return Json::object();
}

Reply exploreClose(State& s, Params& p) {
  const std::string handle = p.string("handle");
  BTE_CHECK(p.finish());
  if (const std::optional<std::uint64_t> a = parseId(IdKind::AssemblyExplorer, handle)) {
    if (s.assemblyExplorers.erase(*a) == 0) {
      return coreError(Errc::NotFound, "there is no such explorer");
    }
  } else if (const std::optional<std::uint64_t> m = parseId(IdKind::MovementExplorer, handle)) {
    if (s.movementExplorers.erase(*m) == 0) {
      return coreError(Errc::NotFound, "there is no such explorer");
    }
  } else {
    return paramError("'handle' must be an explorer's handle (ax1, mx1, ...)");
  }
  return Json::object();
}

}  // namespace

void addExploreMethods(MethodTable& t) {
  t["explore.openAssembly"] = exploreOpenAssembly;
  t["explore.placements"] = explorePlacements;
  t["explore.step"] = exploreStep;
  t["explore.openMovements"] = exploreOpenMovements;
  t["explore.node"] = exploreNode;
  t["explore.analyse"] = exploreAnalyse;
  t["explore.addMove"] = exploreAddMove;
  t["explore.findMatching"] = exploreFindMatching;
  t["explore.prune"] = explorePrune;
  t["explore.close"] = exploreClose;
}

}  // namespace bt::engine
