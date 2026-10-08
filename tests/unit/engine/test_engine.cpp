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
#include <cstring>
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
  {
    Engine e(out, log);
    std::istringstream in(R"({"jsonrpc":"2.0","id":1,"method":"settings.get"})"
                          "\n");
    CHECK(e.run(in));
    CHECK_FALSE(e.stopped());
  }
  CHECK(parse(out.str()).messages.size() == 1);
  out.str("");
  {
    Engine e(out, log);
    std::istringstream in(R"({"jsonrpc":"2.0","id":1,"method":"engine.shutdown"})"
                          "\n"
                          R"({"jsonrpc":"2.0","id":2,"method":"settings.get"})"
                          "\n");
    (void)e.run(in);
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
