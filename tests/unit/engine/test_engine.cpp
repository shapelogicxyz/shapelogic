// SPDX-License-Identifier: GPL-3.0-or-later
// The engine in process (decision 0012): the params reader, ids, base64,
// frames, and the dispatcher's replies, errors and events on a captured
// output. The contract tests (tests/contract/) drive the real process.
#include "engine.h"
#include "output.h"
#include "rpc.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <sstream>
#include <thread>
#include <string>
#include <vector>

using namespace bt;
using namespace bt::engine;

namespace {

// The messages an engine wrote, frames kept apart.
struct Captured {
  std::vector<Json> messages;
  std::vector<std::string> frames;
};

Captured parse(const std::string& out) {
  Captured c;
  std::size_t at = 0;
  while (at < out.size()) {
    const std::size_t nl = out.find('\n', at);
    REQUIRE(nl != std::string::npos);
    Json m = Json::parse(out.substr(at, nl - at));
    at = nl + 1;
    if (m.contains("method") && m["method"] == "frame") {
      const auto n = m["params"]["bytes"].get<std::size_t>();
      REQUIRE(at + n <= out.size());
      c.frames.push_back(out.substr(at, n));
      at += n;
    }
    c.messages.push_back(std::move(m));
  }
  return c;
}

// An engine on a captured output.
struct Harness {
  std::ostringstream out;
  std::ostringstream log;
  Engine engine{out, log};
  std::uint64_t next = 1;

  // A call: the messages it wrote, the reply last before its events.
  Captured call(const std::string& method, const Json& params = Json::object()) {
    out.str("");
    Json m = {{"jsonrpc", "2.0"}, {"id", next++}, {"method", method}, {"params", params}};
    engine.handleLine(m.dump());
    return parse(out.str());
  }

  static const Json& reply(const Captured& c) {
    for (const Json& m : c.messages) {
      if (m.contains("id")) {
        return m;
      }
    }
    FAIL("no reply");
    static const Json none;
    return none;
  }

  Json result(const std::string& method, const Json& params = Json::object()) {
    const Captured c = call(method, params);
    const Json& r = reply(c);
    INFO(r.dump());
    REQUIRE(r.contains("result"));
    return r["result"];
  }

  std::string errc(const std::string& method, const Json& params = Json::object()) {
    const Captured c = call(method, params);
    const Json& r = reply(c);
    INFO(r.dump());
    REQUIRE(r.contains("error"));
    return r["error"]["data"]["errc"].get<std::string>();
  }
};

}  // namespace

TEST_CASE("engine: ids carry their kind", "[engine]") {
  CHECK(formatId(IdKind::Shape, 7) == "s7");
  CHECK(formatId(IdKind::Solution, 42) == "sol42");
  CHECK(parseId(IdKind::Shape, "s7") == 7U);
  CHECK(parseId(IdKind::Solution, "sol42") == 42U);
  CHECK_FALSE(parseId(IdKind::Shape, "sol42"));
  CHECK_FALSE(parseId(IdKind::Shape, "c7"));
  CHECK_FALSE(parseId(IdKind::Shape, "s07"));
  CHECK_FALSE(parseId(IdKind::Shape, "s"));
  CHECK_FALSE(parseId(IdKind::Shape, "s7x"));
  CHECK_FALSE(parseId(IdKind::Shape, "s99999999999999999999999"));
  CHECK(idKindOf("sol3") == IdKind::Solution);
  CHECK(idKindOf("mx1") == IdKind::MovementExplorer);
  CHECK_FALSE(idKindOf("q1"));
}

TEST_CASE("engine: params are read strictly", "[engine]") {
  SECTION("types and ranges") {
    const Json j = {{"a", "text"}, {"n", 5}, {"f", 1.5}, {"b", true}, {"s", "s3"}};
    Params p(j);
    CHECK(p.string("a") == "text");
    CHECK(p.integer("n", 0, 10) == 5);
    CHECK(p.number("f", 0, 2) == 1.5);
    CHECK(p.boolean("b"));
    CHECK(p.id("s", IdKind::Shape) == 3U);
    CHECK(p.optInteger("absent", 9, 0, 10) == 9);
    CHECK(p.finish().has_value());
  }
  SECTION("the first problem is reported") {
    const Json j = {{"n", 50}, {"a", 3}};
    Params p(j);
    (void)p.integer("n", 0, 10);
    (void)p.string("a");
    const RpcResult<void> r = p.finish();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == kInvalidParamsCode);
    CHECK(r.error().message.find("'n'") != std::string::npos);
  }
  SECTION("unknown and missing fields") {
    const Json j = {{"a", "x"}, {"typo", 1}};
    Params p(j);
    (void)p.string("a");
    REQUIRE_FALSE(p.finish().has_value());
    const Json none = Json::object();
    Params q(none);
    (void)q.string("a");
    REQUIRE_FALSE(q.finish().has_value());
  }
  SECTION("floats are not integers, and ids are of their kind") {
    const Json j = {{"n", 3.0}, {"s", "c3"}};
    Params p(j);
    (void)p.integer("n", 0, 10);
    REQUIRE_FALSE(p.finish().has_value());
    Params q(j);
    (void)q.id("s", IdKind::Shape);
    (void)q.optNumber("n", 0, 0, 10);
    REQUIRE_FALSE(q.finish().has_value());
  }
}

TEST_CASE("engine: base64 both ways", "[engine]") {
  for (const std::string& s : std::vector<std::string>{"", "a", "ab", "abc", "abcd", std::string("\0\xff\x10z", 4)}) {
    const std::string e = base64Encode(s);
    CHECK(e.size() % 4 == 0);
    CHECK(base64Decode(e) == s);
  }
  CHECK(base64Encode("Man") == "TWFu");
  CHECK(base64Encode("Ma") == "TWE=");
  CHECK(base64Decode("TWE") == "Ma");
  CHECK_FALSE(base64Decode("T@E="));
  CHECK_FALSE(base64Decode("T"));
}

TEST_CASE("engine: frames are typed arrays at multiples of four", "[engine]") {
  FrameBuilder f;
  const std::vector<std::uint8_t> bytes = {1, 2, 3};
  const std::vector<float> floats = {1.0F, -2.5F};
  f.add("b", bytes);
  f.add("f", floats);
  const Json d = f.describe(5);
  CHECK(d["frame"] == 5);
  CHECK(d["arrays"]["b"]["offset"] == 0);
  CHECK(d["arrays"]["b"]["length"] == 3);
  CHECK(d["arrays"]["f"]["offset"] == 4);
  CHECK(d["arrays"]["f"]["type"] == "f32");
  CHECK(d["bytes"] == 12);
  float back = 0;
  std::memcpy(&back, f.bytes().data() + 8, 4);
  CHECK(back == -2.5F);
}

TEST_CASE("engine: replies, errors and document.changed", "[engine]") {
  Harness h;
  // malformed lines
  h.out.str("");
  h.engine.handleLine("{not json");
  Captured c = parse(h.out.str());
  REQUIRE(c.messages.size() == 1);
  CHECK(c.messages[0]["error"]["code"] == kParseErrorCode);
  CHECK(c.messages[0]["id"].is_null());
  CHECK(h.errc("no.method") == "method_not_found");
  CHECK(h.errc("shape.get", {{"shape", "p1"}}) == "invalid_params");

  const Json snap = h.result("document.new", {{"grid", "bricks"}});
  CHECK(snap["grid"] == "bricks");
  const std::uint64_t rev = snap["revision"].get<std::uint64_t>();

  c = h.call("shape.create", {{"size", {2, 2, 2}}});
  REQUIRE(c.messages.size() == 2);
  // the reply first, then its event
  CHECK(c.messages[0]["result"]["shape"] == "s1");
  CHECK(c.messages[1]["method"] == "document.changed");
  CHECK(c.messages[1]["params"]["revision"] == c.messages[0]["result"]["revision"]);
  CHECK(c.messages[1]["params"]["revision"].get<std::uint64_t>() > rev);
  CHECK(c.messages[1]["params"]["shapes"] == Json::array({"s1"}));

  // a call that changes nothing sends no event
  c = h.call("shape.rename", {{"shape", "s1"}, {"name", ""}});
  CHECK(c.messages.size() == 1);
  CHECK(h.errc("shape.get", {{"shape", "s2"}}) == "not_found");
  CHECK(h.errc("edit.end") == "invalid_state");

  // a mesh: the frame before the reply, as long as the reply says
  h.result("shape.setCells", {{"shape", "s1"}, {"tool", "fixed"}, {"cells", {{0, 0, 0}}}});
  c = h.call("shape.mesh", {{"shape", "s1"}});
  REQUIRE(c.messages.size() == 2);
  CHECK(c.messages[0]["method"] == "frame");
  REQUIRE(c.frames.size() == 1);
  const Json& mesh = c.messages[1]["result"];
  CHECK(mesh["frame"]["frame"] == c.messages[0]["params"]["frame"]);
  CHECK(mesh["frame"]["bytes"] == c.frames[0].size());
  CHECK(mesh["triangles"].get<int>() > 0);

  // revisions never go back across documents
  const std::uint64_t before = h.result("document.get")["revision"].get<std::uint64_t>();
  CHECK(h.result("document.new", {{"grid", "spheres"}})["revision"].get<std::uint64_t>() > before);

  // a notification: no reply
  h.out.str("");
  h.engine.handleLine(R"({"jsonrpc":"2.0","method":"document.setNotes","params":{"text":"x"}})");
  c = parse(h.out.str());
  REQUIRE(c.messages.size() == 1);
  CHECK(c.messages[0]["method"] == "document.changed");
}

TEST_CASE("engine: the run loop ends at the end of input or on shutdown", "[engine][parallel]") {
  std::ostringstream out;
  std::ostringstream log;
  // the streams outlive the test: a reader thread the engine had to leave
  // behind (it waits 100 ms for it) must not find them gone
  static std::istringstream in1(R"({"jsonrpc":"2.0","id":1,"method":"settings.get"})"
                                "\n");
  static std::istringstream in2(R"({"jsonrpc":"2.0","id":1,"method":"engine.shutdown"})"
                                "\n"
                                R"({"jsonrpc":"2.0","id":2,"method":"settings.get"})"
                                "\n");
  {
    Engine e(out, log);
    CHECK(e.run(in1));
    CHECK_FALSE(e.stopped());
  }
  CHECK(parse(out.str()).messages.size() == 1);
  out.str("");
  {
    Engine e(out, log);
    (void)e.run(in2);
    CHECK(e.stopped());
  }
  // nothing after the shutdown runs
  const Captured c = parse(out.str());
  REQUIRE(c.messages.size() == 1);
  CHECK(c.messages[0]["id"] == 1);
}

TEST_CASE("engine: a solve's events, polled while it runs", "[engine][parallel]") {
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});
  const std::string box = h.result("shape.create", {{"size", {2, 4, 4}}})["shape"];
  h.result("shape.setBox", {{"shape", box}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {1, 3, 3}}});
  const std::string domino = h.result("shape.create", {{"size", {2, 1, 1}}})["shape"];
  h.result("shape.setBox", {{"shape", domino}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {1, 0, 0}}});
  const std::string p = h.result("problem.create")["problem"];
  h.result("problem.setResult", {{"problem", p}, {"shape", box}});
  h.result("problem.setPieceCount", {{"problem", p}, {"shape", domino}, {"min", 16}, {"max", 16}});
  h.result("solve.start", {{"problem", p}, {"options", {{"disassemble", true}, {"threads", 4}}}});

  // the owner's loop: requests and polls while the solve's threads run
  std::vector<Json> events;
  bool finished = false;
  for (int i = 0; i < 20000 && !finished; i++) {
    h.out.str("");
    h.engine.poll();
    (void)h.engine.busy();
    for (Json& m : parse(h.out.str()).messages) {
      if (m["method"] == "solve.state" && m["params"]["state"] == "finished") {
        finished = true;
      }
      events.push_back(std::move(m));
    }
    if (i % 10 == 0) {
      (void)h.result("problem.get", {{"problem", p}});
      (void)h.result("solution.page", {{"problem", p}, {"limit", 5}});
      (void)h.result("solve.status", {{"problem", p}});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(finished);
  // the run's end gives a revision
  h.out.str("");
  h.engine.poll();
  for (Json& m : parse(h.out.str()).messages) {
    events.push_back(std::move(m));
  }
  const auto count = [&events](const char* method) {
    return std::count_if(events.begin(), events.end(),
                         [method](const Json& m) { return m["method"] == method; });
  };
  CHECK(count("solve.progress") >= 1);
  CHECK(count("solve.solutionsAdded") >= 1);
  CHECK(count("document.changed") >= 1);
  const Json detail = h.result("problem.get", {{"problem", p}});
  CHECK(detail["assemblies"] == 2856);
  CHECK(detail["state"] == "solved");
  CHECK_FALSE(h.engine.busy());
}

TEST_CASE("engine: a run that ends before the next poll still tells its end", "[engine]") {
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});
  const std::string box = h.result("shape.create", {{"size", {2, 1, 1}}})["shape"];
  h.result("shape.setBox", {{"shape", box}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {1, 0, 0}}});
  const std::string domino = h.result("shape.create", {{"size", {2, 1, 1}}})["shape"];
  h.result("shape.setBox", {{"shape", domino}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {1, 0, 0}}});
  const std::string p = h.result("problem.create")["problem"];
  h.result("problem.setResult", {{"problem", p}, {"shape", box}});
  h.result("problem.setPieceCount", {{"problem", p}, {"shape", domino}, {"min", 1}, {"max", 1}});
  const app::Id id = *parseId(IdKind::Problem, p);
  for (int run = 0; run < 3; run++) {
    h.result("solve.start", {{"problem", p}});
    // the whole run happens before the engine looks
    REQUIRE(h.engine.state().document().solveWait(id).has_value());
    h.out.str("");
    h.engine.poll();
    const Captured c = parse(h.out.str());
    const bool told = std::any_of(c.messages.begin(), c.messages.end(), [](const Json& m) {
      return m["method"] == "solve.state" && m["params"]["state"] == "finished";
    });
    INFO("run " << run);
    CHECK(told);
  }
}

TEST_CASE("engine: a deeply nested request is refused, not run", "[engine]") {
  Harness h;
  const auto nested = [](int depth) {
    return std::string(R"({"jsonrpc":"2.0","id":1,"method":"settings.get","params":{"x":)") +
           std::string(static_cast<std::size_t>(depth), '[') +
           std::string(static_cast<std::size_t>(depth), ']') + "}}";
  };
  h.out.str("");
  h.engine.handleLine(nested(50000));
  Captured c = parse(h.out.str());
  REQUIRE(c.messages.size() == 1);
  CHECK(c.messages[0]["error"]["code"] == kInvalidRequestCode);
  CHECK(c.messages[0]["error"]["message"] == "nesting too deep");
  CHECK(c.messages[0]["id"].is_null());
  // within the limit the line is a request like any other: its unknown
  // field is what gets refused
  h.out.str("");
  h.engine.handleLine(nested(50));
  c = parse(h.out.str());
  REQUIRE(c.messages.size() == 1);
  CHECK(c.messages[0]["error"]["data"]["errc"] == "invalid_params");
  // and the engine goes on
  CHECK(h.result("settings.get")["undoDepth"] == 25);
}

TEST_CASE("engine: a notification cannot be given a frame", "[engine]") {
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});
  h.result("shape.create", {{"size", {2, 2, 2}}});
  h.result("shape.setCells", {{"shape", "s1"}, {"tool", "fixed"}, {"cells", {{0, 0, 0}}}});
  h.out.str("");
  h.log.str("");
  h.engine.handleLine(R"({"jsonrpc":"2.0","method":"shape.mesh","params":{"shape":"s1"}})");
  CHECK(h.out.str().empty());
  CHECK(h.log.str().find("shape.mesh") != std::string::npos);
  CHECK(h.log.str().find("notification") != std::string::npos);
  // the next request's frame is the process's first
  const Captured c = h.call("shape.mesh", {{"shape", "s1"}});
  REQUIRE(c.messages.size() == 2);
  CHECK(c.messages[0]["params"]["frame"] == 1);
  CHECK(c.messages[1]["result"]["frame"]["frame"] == 1);
}

TEST_CASE("engine: a line over the limit is refused while it is read", "[engine][parallel]") {
  std::ostringstream out;
  std::ostringstream log;
  static std::istringstream in;
  {
    std::string input(Engine::kMaxLine + 1, 'x');
    input += "\n";
    input += R"({"jsonrpc":"2.0","id":7,"method":"settings.get"})"
             "\n";
    in.str(input);
  }
  Engine e(out, log);
  CHECK(e.run(in));
  const Captured c = parse(out.str());
  REQUIRE(c.messages.size() == 2);
  CHECK(c.messages[0]["error"]["code"] == kInvalidRequestCode);
  CHECK(c.messages[0]["error"]["message"] == "the line is too long");
  CHECK(c.messages[0]["id"].is_null());
  CHECK(c.messages[1]["id"] == 7);
  CHECK(c.messages[1].contains("result"));
}

TEST_CASE("engine: engine.hello names the protocol", "[engine]") {
  Harness h;
  const Json r = h.result("engine.hello", {{"protocol", 1}});
  CHECK(r["protocol"] == 1);
  CHECK(r["name"] == "bt-engine");
  CHECK(h.errc("engine.hello", {{"protocol", 2}}) == "unsupported");
  CHECK(h.errc("engine.hello", {{"protocol", 0}}) == "invalid_params");
  CHECK(h.errc("engine.hello") == "invalid_params");
}

TEST_CASE("engine: solve.abort is the stop of a solve under way", "[engine]") {
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});
  const std::string box = h.result("shape.create", {{"size", {2, 1, 1}}})["shape"];
  h.result("shape.setBox", {{"shape", box}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {1, 0, 0}}});
  const std::string domino = h.result("shape.create", {{"size", {2, 1, 1}}})["shape"];
  h.result("shape.setBox", {{"shape", domino}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {1, 0, 0}}});
  const std::string p = h.result("problem.create")["problem"];
  h.result("problem.setResult", {{"problem", p}, {"shape", box}});
  h.result("problem.setPieceCount", {{"problem", p}, {"shape", domino}, {"min", 1}, {"max", 1}});
  const app::Id id = *parseId(IdKind::Problem, p);
  // nothing under way: refused
  CHECK(h.errc("solve.abort", {{"problem", p}}) == "invalid_state");
  // a finished run: refused, the solutions stay; removing them is a step
  h.result("solve.start", {{"problem", p}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  h.engine.poll();
  CHECK(h.errc("solve.abort", {{"problem", p}}) == "invalid_state");
  CHECK(h.result("problem.get", {{"problem", p}})["saved"] == 1);
  h.result("solution.delete", {{"problem", p}, {"which", "all"}});
  CHECK(h.result("problem.get", {{"problem", p}})["saved"] == 0);
  h.result("edit.undo");
  CHECK(h.result("problem.get", {{"problem", p}})["saved"] == 1);
  // a paused one is a solve under way
  h.result("solve.prepare", {{"problem", p}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  h.engine.poll();
  CHECK(h.result("problem.get", {{"problem", p}})["solve"] == "paused");
  h.result("solve.abort", {{"problem", p}});
  const Json after = h.result("problem.get", {{"problem", p}});
  CHECK(after["solve"] == "idle");
  CHECK(after["state"] == "unsolved");
}

namespace {

// Builds a box of x*y*z cells to fill with dominoes; returns the problem id.
std::string dominoBox(Harness& h, int x, int y, int z) {
  const std::string box = h.result("shape.create", {{"size", {x, y, z}}})["shape"];
  h.result("shape.setBox", {{"shape", box}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {x - 1, y - 1, z - 1}}});
  const std::string domino = h.result("shape.create", {{"size", {2, 1, 1}}})["shape"];
  h.result("shape.setBox", {{"shape", domino}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {1, 0, 0}}});
  const std::string p = h.result("problem.create")["problem"];
  h.result("problem.setResult", {{"problem", p}, {"shape", box}});
  const int n = x * y * z / 2;
  h.result("problem.setPieceCount", {{"problem", p}, {"shape", domino}, {"min", n}, {"max", n}});
  return p;
}

// Polls (and runs `between` every tenth round) until `done` sees an event
// or `rounds` rounds have passed; returns the events seen.
template <class Done, class Between>
std::vector<Json> pollUntil(Harness& h, int rounds, Done&& done, Between&& between) {
  std::vector<Json> events;
  bool finished = false;
  for (int i = 0; i < rounds && !finished; i++) {
    h.out.str("");
    h.engine.poll();
    for (Json& m : parse(h.out.str()).messages) {
      finished = finished || done(m);
      events.push_back(std::move(m));
    }
    if (i % 10 == 0) {
      between();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return events;
}

}  // namespace

// The messages of one poll whose taking in and telling have `between` in
// the middle (work made to end inside the poll).
std::vector<Json> pollWith(Harness& h, const std::function<void()>& between) {
  h.out.str("");
  pollEvents(h.engine.state(), std::chrono::steady_clock::now(), between);
  return parse(h.out.str()).messages;
}

// Where the first message satisfying `f` is in `ms`, or -1.
template <class F>
std::ptrdiff_t indexOf(const std::vector<Json>& ms, F&& f) {
  const auto it = std::find_if(ms.begin(), ms.end(), std::forward<F>(f));
  return it == ms.end() ? -1 : it - ms.begin();
}

TEST_CASE("engine: a run that ends inside a poll is told before the revision of its end",
          "[engine][parallel]") {
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});
  const std::string p = dominoBox(h, 2, 4, 5);
  const app::Id id = *parseId(IdKind::Problem, p);
  h.result("solve.start", {{"problem", p}, {"options", {{"disassemble", true}, {"threads", 1}}}});
  (void)pollUntil(
      h, 60000,
      [&](const Json& m) {
        return m["method"] == "solve.state" && m["params"]["state"] == "assembling";
      },
      [] {});
  // the run pauses and has ended while the engine polls: between taking in
  // what ended and telling the states
  std::vector<Json> ms = pollWith(h, [&] {
    REQUIRE(h.engine.state().document().solvePause(id).has_value());
    REQUIRE(h.engine.state().document().solveWait(id).has_value());
  });
  for (Json& m : pollWith(h, {})) {
    ms.push_back(std::move(m));
  }
  const std::ptrdiff_t state = indexOf(ms, [](const Json& m) {
    return m["method"] == "solve.state" && m["params"]["state"] == "paused";
  });
  const std::ptrdiff_t progress = indexOf(ms, [](const Json& m) {
    return m["method"] == "solve.progress" && m["params"]["state"] == "paused";
  });
  const std::ptrdiff_t changed = indexOf(ms, [&](const Json& m) {
    return m["method"] == "document.changed" &&
           std::find(m["params"]["problems"].begin(), m["params"]["problems"].end(), p) !=
               m["params"]["problems"].end();
  });
  INFO(Json(ms).dump());
  REQUIRE(state >= 0);
  REQUIRE(changed >= 0);
  CHECK(state < progress);
  CHECK(progress < changed);
  h.result("solve.abort", {{"problem", p}});
}

TEST_CASE("engine: an analysis that ends inside a poll is told, with its counts, before its results",
          "[engine][parallel]") {
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});
  const std::string p = dominoBox(h, 2, 3, 4);
  const app::Id id = *parseId(IdKind::Problem, p);
  h.result("solve.start", {{"problem", p}, {"options", {{"keepAtMost", 0}, {"threads", 1}}}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  (void)pollWith(h, {});
  const std::uint64_t total = h.result("problem.get", {{"problem", p}})["saved"];
  REQUIRE(total > 10);
  REQUIRE(h.result("solution.analyse", {{"problem", p}, {"which", "all"}, {"threads", 1}})["total"] ==
          total);
  // the analysis ends while the engine polls
  std::vector<Json> ms = pollWith(h, [&] {
    for (int i = 0; i < 60000 && h.engine.state().document().analyseProgress(id)->active; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  for (Json& m : pollWith(h, {})) {
    ms.push_back(std::move(m));
  }
  const std::ptrdiff_t ended =
      indexOf(ms, [](const Json& m) { return m["method"] == "solution.analyseEnded"; });
  const std::ptrdiff_t changed = indexOf(ms, [&](const Json& m) {
    return m["method"] == "document.changed" &&
           std::find(m["params"]["problems"].begin(), m["params"]["problems"].end(), p) !=
               m["params"]["problems"].end();
  });
  INFO(Json(ms).dump());
  REQUIRE(ended >= 0);
  REQUIRE(changed >= 0);
  CHECK(ended < changed);
  const Json& e = ms[static_cast<std::size_t>(ended)]["params"];
  CHECK(e["total"] == total);
  CHECK(e["done"] == total);
  CHECK(e["separated"] == total);
  CHECK(e["stopped"] == false);
  // the results are in
  for (const Json& r : h.result("solution.page", {{"problem", p}, {"limit", 1000}})["rows"]) {
    CHECK(r["disassembly"] == "tree");
  }
  // asked after it, the analysis tells how it ended
  const Json st = h.result("solution.analyseStatus", {{"problem", p}});
  CHECK(st["active"] == false);
  CHECK(st["done"] == total);
}

TEST_CASE("engine: a solve next to an analysis of another problem", "[engine][parallel]") {
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});
  const std::string a = dominoBox(h, 2, 4, 4);
  const std::string b = h.result("problem.duplicate", {{"problem", a}})["problem"];
  const std::string box = h.result("problem.get", {{"problem", a}})["result"];
  h.result("solve.start", {{"problem", a}, {"options", {{"disassemble", true}, {"keepAtMost", 0}, {"threads", 2}}}});
  (void)pollUntil(
      h, 60000,
      [](const Json& m) { return m["method"] == "solve.state" && m["params"]["state"] == "finished"; },
      [] {});
  REQUIRE(h.result("problem.get", {{"problem", a}})["saved"] == 2856);

  // a's solutions taken apart again while b is solved; the owner reads both
  REQUIRE(h.result("solution.analyse", {{"problem", a}, {"which", "all"}, {"threads", 2}})["total"] == 2856);
  h.result("solve.start", {{"problem", b}, {"options", {{"disassemble", true}, {"keepAtMost", 0}, {"threads", 2}}}});
  bool analysed = false;
  bool solved = false;
  const std::vector<Json> events = pollUntil(
      h, 120000,
      [&](const Json& m) {
        if (m["method"] == "solution.analyseEnded" && m["params"]["problem"] == a) {
          analysed = true;
          CHECK(m["params"]["done"] == 2856);
          CHECK(m["params"]["separated"] == 2856);
          CHECK(m["params"]["failure"].is_null());
        }
        if (m["method"] == "solve.state" && m["params"]["problem"] == b &&
            m["params"]["state"] == "finished") {
          solved = true;
        }
        return analysed && solved;
      },
      [&] {
        (void)h.result("solution.page", {{"problem", a}, {"limit", 20}});
        (void)h.result("solution.page", {{"problem", b}, {"sortBy", "moves"}, {"limit", 20}});
        (void)h.result("shape.table");
        (void)h.result("shape.mesh", {{"shape", box}});
        (void)h.result("problem.get", {{"problem", a}});
        (void)h.result("solve.status", {{"problem", b}});
        (void)h.result("solution.analyseStatus", {{"problem", a}});
        (void)h.result("document.get");
      });
  REQUIRE(analysed);
  REQUIRE(solved);
  h.engine.poll();
  CHECK(h.result("problem.get", {{"problem", b}})["assemblies"] == 2856);
  CHECK_FALSE(h.engine.busy());
}

TEST_CASE("engine: explorers, fit, paging and the table while a solve runs; a new document ends it",
          "[engine][parallel]") {
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});
  const std::string p = dominoBox(h, 2, 4, 6);
  h.result("solve.start", {{"problem", p}, {"options", {{"disassemble", true}, {"keepAtMost", 10}, {"threads", 2}}}});
  int rounds = 0;
  const std::vector<Json> events = pollUntil(
      h, 400,
      [](const Json& m) { return m["method"] == "solve.state" && m["params"]["state"] == "finished"; },
      [&] {
        rounds++;
        (void)h.result("solve.status", {{"problem", p}});
        (void)h.result("problem.fit", {{"problem", p}});
        (void)h.result("problem.kind", {{"problem", p}});
        const std::string ax = h.result("explore.openAssembly", {{"problem", p}})["handle"];
        (void)h.result("explore.step", {{"handle", ax}, {"steps", 50}});
        h.result("explore.close", {{"handle", ax}});
        (void)h.result("solution.page", {{"problem", p}, {"sortBy", "moves"}, {"descending", true}});
        (void)h.result("problem.list");
        (void)h.result("shape.table");
      });
  CHECK(rounds >= 10);
  // the solve is still under way (a 2x4x6 box takes far longer): a new
  // document ends it and tells the problem goodbye after the reply
  REQUIRE(h.engine.busy());
  const Captured c = h.call("document.new", {{"grid", "bricks"}});
  REQUIRE(c.messages.size() >= 2);
  CHECK(c.messages[0]["result"]["problems"] == Json::array());
  const bool farewell = std::any_of(c.messages.begin() + 1, c.messages.end(), [&p](const Json& m) {
    return m["method"] == "solve.state" && m["params"]["problem"] == p && m["params"]["state"] == "idle";
  });
  CHECK(farewell);
  CHECK_FALSE(h.engine.busy());
  CHECK(h.errc("solve.status", {{"problem", p}}) == "not_found");
}

namespace {

// The sides a mesh draws for one cell: voxelFace of its triangles (on the
// sphere grid not the connection holes, flag 1 there, whose number is stale).
std::vector<int> sidesOf(Harness& h, const std::string& shape, std::uint32_t voxel, bool spheres) {
  const Captured c = h.call("shape.mesh", {{"shape", shape}});
  REQUIRE(c.frames.size() == 1);
  const Json& arrays = Harness::reply(c)["result"]["frame"]["arrays"];
  const auto read = [&](const char* name, std::size_t i) {
    std::uint32_t v = 0;
    std::memcpy(&v, c.frames[0].data() + arrays[name]["offset"].get<std::size_t>() + 4 * i, 4);
    return v;
  };
  std::vector<int> sides;
  for (std::size_t t = 0; t < arrays["voxelFace"]["length"].get<std::size_t>(); t++) {
    const auto face = static_cast<std::int32_t>(read("voxelFace", t));
    if (read("voxel", t) == voxel && face >= 0 && (!spheres || (read("flags", t) & 1U) == 0) &&
        std::find(sides.begin(), sides.end(), face) == sides.end()) {
      sides.push_back(face);
    }
  }
  std::sort(sides.begin(), sides.end());
  return sides;
}

}  // namespace

TEST_CASE("engine: grid.neighbour numbers the sides as the meshes do", "[engine]") {
  Harness h;
  for (const char* grid : {"bricks", "prisms", "spheres", "rhombic", "tetraOcta"}) {
    INFO(grid);
    h.result("document.new", {{"grid", grid}});
    const Json size = Json::array({10, 10, 10});
    const std::string s = h.result("shape.create", {{"size", size}})["shape"];
    // a cell of the middle layer near the middle of the space
    const Json geo = h.result("grid.geometry", {{"size", size}, {"layer", 5}});
    Json cell;
    int best = 1 << 30;
    for (const Json& c : geo["cells"]) {
      const int d = std::abs(c["at"][0].get<int>() - 5) + std::abs(c["at"][1].get<int>() - 5);
      if (d < best) {
        best = d;
        cell = c["at"];
      }
    }
    REQUIRE(cell.is_array());
    h.result("shape.setCells", {{"shape", s}, {"tool", "fixed"}, {"cells", {cell}}});
    const auto voxel =
        cell[0].get<std::uint32_t>() + 10 * (cell[1].get<std::uint32_t>() + 10 * cell[2].get<std::uint32_t>());
    const bool spheres = std::string(grid) == "spheres";
    const std::vector<int> sides = sidesOf(h, s, voxel, spheres);
    REQUIRE(sides.size() >= 4);
    for (const int face : sides) {
      INFO("side " << face);
      const Json n = h.result("grid.neighbour", {{"size", size}, {"cell", cell}, {"face", face}})["cell"];
      REQUIRE(n.is_array());
      // the cell across the side, filled, takes exactly that side away
      CHECK(h.result("shape.setCells", {{"shape", s}, {"tool", "fixed"}, {"cells", {n}}})["changed"] == 1);
      std::vector<int> rest = sides;
      rest.erase(std::find(rest.begin(), rest.end(), face));
      CHECK(sidesOf(h, s, voxel, spheres) == rest);
      h.result("edit.undo");
    }
    CHECK(h.errc("grid.neighbour", {{"size", size}, {"cell", cell}, {"face", 99}}) == "invalid_argument");
    CHECK(h.errc("grid.neighbour", {{"size", size}, {"cell", {10, 0, 0}}, {"face", 0}}) ==
          "invalid_argument");
  }
  // across the edge of the space: none; a cell the grid does not have
  h.result("document.new", {{"grid", "bricks"}});
  CHECK(h.result("grid.neighbour", {{"size", {2, 2, 2}}, {"cell", {0, 0, 0}}, {"face", 0}})["cell"].is_null());
  CHECK(h.result("grid.neighbour", {{"size", {2, 2, 2}}, {"cell", {0, 0, 0}}, {"face", 3}})["cell"] ==
        Json::array({1, 0, 0}));
  h.result("document.new", {{"grid", "spheres"}});
  CHECK(h.errc("grid.neighbour", {{"size", {3, 3, 3}}, {"cell", {1, 0, 0}}, {"face", 0}}) ==
        "invalid_argument");
}

TEST_CASE("engine: every change inside an edit group has its revision and its event", "[engine]") {
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});
  const std::string s = h.result("shape.create", Json::object())["shape"];
  CHECK(h.call("edit.begin", {{"label", "Draw"}}).messages.size() == 1);
  std::uint64_t last = h.result("document.get")["revision"].get<std::uint64_t>();
  // a drag's batches on one shape: each moves the revision and says so
  for (int i = 0; i < 3; i++) {
    const Captured c = h.call("shape.setCells", {{"shape", s}, {"tool", "fixed"}, {"cells", {{i, 0, 0}}}});
    REQUIRE(c.messages.size() == 2);
    const auto rev = c.messages[0]["result"]["revision"].get<std::uint64_t>();
    CHECK(rev == last + 1);
    CHECK(c.messages[1]["method"] == "document.changed");
    CHECK(c.messages[1]["params"]["revision"] == rev);
    CHECK(c.messages[1]["params"]["shapes"] == Json::array({s}));
    last = rev;
  }
  // a batch that changes nothing: no event
  CHECK(h.call("shape.setCells", {{"shape", s}, {"tool", "fixed"}, {"cells", {{0, 0, 0}}}}).messages.size() == 1);
  h.result("edit.end");
  CHECK(h.result("edit.history")["undo"].back() == "Draw");
  CHECK(h.result("shape.get", {{"shape", s}})["shape"]["fixed"] == 3);
}
