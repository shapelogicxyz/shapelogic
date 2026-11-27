// SPDX-License-Identifier: GPL-3.0-or-later
// The engine in process (decision 0012): the params reader, ids, base64,
// frames, and the dispatcher's replies, errors and events on a captured
// output. The contract tests (tests/contract/) drive the real process.
#include "engine.h"
#include "output.h"
#include "rpc.h"
#include "../scratch_dir.h"

#include "shapelogic/app/tray_edits.h"
#include "shapelogic/base/assert.h"
#include "shapelogic/model/solution.h"
#include "shapelogic/model/problem.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ostream>
#include <set>
#include <sstream>
#include <streambuf>
#include <thread>
#include <string>
#include <vector>

using namespace sl;
using namespace sl::engine;
using sl::test::ScratchDir;

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

namespace {

// An output that takes `lines` lines and refuses every write after them,
// as a pipe does once its reader is gone (and SIGPIPE is ignored).
class ClosingBuf : public std::streambuf {
 public:
  explicit ClosingBuf(int lines) : left_(lines) {}
  [[nodiscard]] const std::string& taken() const { return taken_; }

 protected:
  int_type overflow(int_type c) override {
    if (left_ <= 0 || traits_type::eq_int_type(c, traits_type::eof())) {
      return traits_type::eof();
    }
    const char ch = traits_type::to_char_type(c);
    taken_ += ch;
    if (ch == '\n') {
      left_--;
    }
    return c;
  }

 private:
  int left_;
  std::string taken_;
};

}  // namespace

// Cleanup item 15.1: an app that crashed or was force-quit leaves the
// engine's output without a reader. The write that fails ends the run
// loop, so the caller lets the engine go and its document pauses the
// solves; nothing more is read or run.
TEST_CASE("engine: the run loop ends when its output is refused", "[engine][parallel]") {
  ClosingBuf buf(1);
  std::ostream out(&buf);
  std::ostringstream log;
  // (static: see the case above)
  static std::istringstream in(R"({"jsonrpc":"2.0","id":1,"method":"settings.get"})"
                               "\n"
                               R"({"jsonrpc":"2.0","id":2,"method":"settings.get"})"
                               "\n"
                               R"({"jsonrpc":"2.0","id":3,"method":"engine.shutdown"})"
                               "\n");
  Engine e(out, log);
  // not at the end of the input: the third line is never run
  CHECK_FALSE(e.run(in));
  CHECK_FALSE(e.stopped());
  const Captured c = parse(buf.taken());
  REQUIRE(c.messages.size() == 1);
  CHECK(c.messages[0]["id"] == 1);
}

// Cleanup item 15.1: an exception outside a method's handler (a failed
// SL_ASSERT in Document::poll, say) used to reach std::terminate, and the
// solves were never paused. It is logged, once while it repeats, and the
// engine goes on answering.
TEST_CASE("engine: a broken invariant in a poll is logged and the run loop goes on",
          "[engine][parallel]") {
  std::ostringstream out;
  std::ostringstream log;
  static std::istringstream in(R"({"jsonrpc":"2.0","id":1,"method":"settings.get"})"
                               "\n"
                               R"({"jsonrpc":"2.0","id":2,"method":"settings.get"})"
                               "\n"
                               R"({"jsonrpc":"2.0","id":3,"method":"settings.get"})"
                               "\n"
                               R"({"jsonrpc":"2.0","id":4,"method":"settings.get"})"
                               "\n");
  Engine e(out, log);
  // the polls after the first, second and fourth reply fail; those after
  // the third are clean (by the replies written, not by counting polls:
  // the loop also polls while it waits for a line)
  e.setPollHook([&out] {
    const std::string written = out.str();
    const auto replies = std::count(written.begin(), written.end(), '\n');
    SL_ASSERT_MSG(replies == 3, "the test broke an invariant");
  });
  CHECK(e.run(in));
  const Captured c = parse(out.str());
  REQUIRE(c.messages.size() == 4);
  for (std::size_t i = 0; i < 4; i++) {
    CHECK(c.messages[i]["id"] == i + 1);
    CHECK(c.messages[i].contains("result"));
  }
  // told once for the first two (the same failure, repeated), and again
  // after the clean poll
  const std::string logged = log.str();
  INFO(logged);
  std::size_t lines = 0;
  for (std::size_t at = logged.find("shapelogic_engine: ERROR: Internal Error: the run loop: ");
       at != std::string::npos;
       at = logged.find("shapelogic_engine: ERROR: Internal Error: the run loop: ", at + 1)) {
    lines++;
  }
  CHECK(lines == 2);
  CHECK(logged.find("the test broke an invariant") != std::string::npos);
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
  // what the solve went through, kept with the problem (the Solver's Details)
  CHECK(detail["searched"] > 0);
  CHECK(detail["searchDepth"] == 0);
  CHECK(detail["triedApart"] == 2856);
  const Json listed = h.result("document.get")["problems"][0];
  CHECK(listed["searched"] == detail["searched"]);
  CHECK(listed["triedApart"] == 2856);
  CHECK_FALSE(h.engine.busy());
}

TEST_CASE("engine: a stacking solve's events, polled while it runs, paused and carried on",
          "[engine][parallel]") {
  // a Panex folder of the test's own (never the user's cache)
  const ScratchDir scratch("engine_panex");
  const std::filesystem::path& work = scratch.path();
  Harness h;
  h.result("settings.set", {{"panexFolder", work.string()}});
  const Json snap =
      h.result("document.open", {{"path", "tests/data/puzzles/stacking/stack_panex_swap_5.xmpuzzle"}});
  const std::string p = snap["problems"][0]["id"];
  const Json options = {{"disassemble", true}, {"threads", 2}};
  h.result("solve.start", {{"problem", p}, {"options", options}});

  // the owner's loop: requests and polls while the search's threads run; a
  // pause asked for at its first progress (it may come after the end)
  std::vector<Json> events;
  bool finished = false;
  bool paused = false;
  bool asked = false;
  for (int i = 0; i < 60000 && !finished; i++) {
    h.out.str("");
    h.engine.poll();
    for (Json& m : parse(h.out.str()).messages) {
      if (m["method"] == "solve.state" && m["params"]["state"] == "finished") {
        finished = true;
      }
      if (m["method"] == "solve.state" && m["params"]["state"] == "paused") {
        paused = true;
      }
      if (!asked && m["method"] == "solve.progress" && m["params"]["progress"]["searched"] > 0) {
        asked = true;
        (void)h.call("solve.pause", {{"problem", p}});
      }
      events.push_back(std::move(m));
    }
    if (paused) {
      // the search saved itself: told, then carried on
      const Json st = h.result("solve.status", {{"problem", p}});
      CHECK(st["savedSearch"].is_object());
      h.result("solve.continue", {{"problem", p}, {"options", options}});
      paused = false;
    }
    if (i % 10 == 0) {
      (void)h.result("problem.get", {{"problem", p}});
      (void)h.result("solution.page", {{"problem", p}, {"limit", 5}});
      (void)h.result("solve.status", {{"problem", p}});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(finished);
  h.out.str("");
  h.engine.poll();
  // every progress says the Panex Solver runs: the release build can search
  // all 343 moves between two ticks, so a progress of the search stage itself
  // need not come, but the one at the end always does
  std::size_t progressed = 0;
  for (const Json& m : events) {
    if (m["method"] == "solve.progress") {
      progressed++;
      CHECK(m["params"]["progress"]["panexSearch"] == true);
    }
  }
  CHECK(progressed > 0);
  const Json page = h.result("solution.page", {{"problem", p}});
  REQUIRE(page["rows"].size() == 1);
  CHECK(page["rows"][0]["moves"] == 343);
  CHECK(h.result("solve.status", {{"problem", p}})["savedSearch"].is_null());
  CHECK_FALSE(h.engine.busy());
}

// Decision 0028: Search memory is what the searches running at once share,
// and a stacking solve whose Panex folder cannot be written pauses before
// its search; solve.status says why, and Continue with a folder that can
// be written solves.
TEST_CASE("engine: shared search memory, and a search folder that cannot be written",
          "[engine]") {
  const ScratchDir scratch("engine_folder");
  const std::filesystem::path& work = scratch.path();
  std::filesystem::create_directories(work);
  const std::filesystem::path file = work / "not-a-folder";
  { std::ofstream(file) << "x"; }

  Harness h;
  h.result("settings.set", {{"searchMemoryGB", 3}});
  CHECK(h.engine.state().memory->total() == 3000000000ULL);
  h.result("settings.set", {{"panexFolder", file.string()}});
  const Json snap = h.result(
      "document.open", {{"path", "tests/data/puzzles/stacking/stack_panex_swap_5.xmpuzzle"}});
  const std::string p = snap["problems"][0]["id"];
  const app::Id id = *parseId(IdKind::Problem, p);
  const Json options = {{"disassemble", true}, {"threads", 1}};
  CHECK(h.result("solve.status", {{"problem", p}})["folderFailure"].is_null());
  h.result("solve.start", {{"problem", p}, {"options", options}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  h.engine.poll();
  Json st = h.result("solve.status", {{"problem", p}});
  CHECK(st["state"] == "paused");
  REQUIRE(st["folderFailure"].is_object());
  CHECK(st["folderFailure"]["folder"] == file.string());
  CHECK_FALSE(st["folderFailure"]["reason"].get<std::string>().empty());
  CHECK(h.engine.state().memory->holders() == 0);

  // another folder, and Continue solves
  h.result("settings.set", {{"panexFolder", (work / "panex").string()}});
  h.result("solve.continue", {{"problem", p}, {"options", options}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  h.engine.poll();
  st = h.result("solve.status", {{"problem", p}});
  CHECK(st["state"] == "finished");
  CHECK(st["folderFailure"].is_null());
  CHECK(h.result("solution.page", {{"problem", p}})["rows"].size() == 1);
}

TEST_CASE("engine: the log line of a Continue without options names the paused solve's",
          "[engine]") {
  // solve.continue without options carries a solve paused in memory on as
  // it ran: its log line names those options, not the defaults
  const ScratchDir scratch("engine_continue_log");
  const std::filesystem::path& work = scratch.path();
  std::filesystem::create_directories(work);
  const std::filesystem::path file = work / "not-a-folder";
  { std::ofstream(file) << "x"; }

  Harness h;
  // a folder that cannot be written pauses the run before it begins
  h.result("settings.set", {{"panexFolder", file.string()}, {"log", "info"}});
  const Json snap = h.result(
      "document.open", {{"path", "tests/data/puzzles/stacking/stack_panex_swap_5.xmpuzzle"}});
  const std::string p = snap["problems"][0]["id"];
  const app::Id id = *parseId(IdKind::Problem, p);
  h.result("solve.start", {{"problem", p}, {"options", {{"disassemble", true}, {"threads", 3}}}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  h.engine.poll();
  REQUIRE(h.result("solve.status", {{"problem", p}})["state"] == "paused");

  h.log.str("");
  h.result("solve.continue", {{"problem", p}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  const std::string log = h.log.str();
  INFO(log);
  const std::size_t at = log.find("Solve Continued: ");
  REQUIRE(at != std::string::npos);
  const std::string line = log.substr(at, log.find('\n', at) - at);
  CHECK(line.find("options {}; ") != std::string::npos);
  CHECK(line.find("threads 3,") != std::string::npos);
  CHECK(line.find("automatic: every hardware thread") == std::string::npos);
}

// Cleanup item 15.7: a Continue without options takes the paused run's
// options, but the folders, the disk and the memory of the settings as
// they are now: a run paused because its folder could not be written goes
// on once another folder is set (it paused again in the old one).
TEST_CASE("engine: a Continue without options works in the folders the settings name now",
          "[engine]") {
  const ScratchDir scratch("engine_continue_folder");
  const std::filesystem::path& work = scratch.path();
  std::filesystem::create_directories(work);
  const std::filesystem::path file = work / "not-a-folder";
  { std::ofstream(file) << "x"; }
  const std::filesystem::path good = work / "panex";

  Harness h;
  h.result("settings.set", {{"panexFolder", file.string()}, {"log", "info"}});
  const Json snap = h.result(
      "document.open", {{"path", "tests/data/puzzles/stacking/stack_panex_swap_5.xmpuzzle"}});
  const std::string p = snap["problems"][0]["id"];
  const app::Id id = *parseId(IdKind::Problem, p);
  h.result("solve.start", {{"problem", p}, {"options", {{"disassemble", true}, {"threads", 3}}}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  h.engine.poll();
  Json st = h.result("solve.status", {{"problem", p}});
  REQUIRE(st["state"] == "paused");
  REQUIRE(st["folderFailure"].is_object());

  h.result("settings.set", {{"panexFolder", good.string()}, {"panexDiskGB", 7}});
  h.log.str("");
  h.result("solve.continue", {{"problem", p}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  h.engine.poll();
  st = h.result("solve.status", {{"problem", p}});
  CHECK(st["state"] == "finished");
  CHECK(st["folderFailure"].is_null());
  CHECK(h.result("solution.page", {{"problem", p}})["rows"].size() == 1);
  // it ran with the paused run's Find Solutions (a stacking run without
  // it is refused) and threads, in the folder set since
  const std::string log = h.log.str();
  INFO(log);
  const std::size_t at = log.find("Solve Continued: ");
  REQUIRE(at != std::string::npos);
  const std::string line = log.substr(at, log.find('\n', at) - at);
  CHECK(line.find("options {}; ") != std::string::npos);
  CHECK(line.find("threads 3,") != std::string::npos);
  CHECK(line.find(good.string()) != std::string::npos);
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
    input += '\n';
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
  CHECK(r["name"] == "shapelogic_engine");
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

TEST_CASE("engine: solve.abort throws away a paused solve from a file", "[engine]") {
  const ScratchDir scratch("engine_abort");
  const std::filesystem::path path = scratch.path() / "engine_abort.xmpuzzle";
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});
  const std::string box = h.result("shape.create", {{"size", {2, 1, 1}}})["shape"];
  h.result("shape.setBox", {{"shape", box}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {1, 0, 0}}});
  const std::string domino = h.result("shape.create", {{"size", {2, 1, 1}}})["shape"];
  h.result("shape.setBox", {{"shape", domino}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {1, 0, 0}}});
  const std::string p = h.result("problem.create")["problem"];
  h.result("problem.setResult", {{"problem", p}, {"shape", box}});
  h.result("problem.setPieceCount", {{"problem", p}, {"shape", domino}, {"min", 1}, {"max", 1}});
  h.result("solve.prepare", {{"problem", p}});
  REQUIRE(h.engine.state().document().solveWait(*parseId(IdKind::Problem, p)).has_value());
  h.engine.poll();
  h.result("document.save", {{"path", path.string()}});
  // reopened: the problem is left solving, with no run in memory (Activity: Search paused)
  const Json snap = h.result("document.open", {{"path", path.string()}});
  const std::string q = snap["problems"][0]["id"];
  const Json before = h.result("problem.get", {{"problem", q}});
  REQUIRE(before["state"] == "solving");
  CHECK(before["solve"] == "idle");
  h.result("solve.abort", {{"problem", q}});
  const Json after = h.result("problem.get", {{"problem", q}});
  CHECK(after["state"] == "unsolved");
  CHECK(after["saved"] == 0);
  CHECK_FALSE(after["savedPosition"].get<bool>());
  // nothing is left to abort
  CHECK(h.errc("solve.abort", {{"problem", q}}) == "invalid_state");
}

TEST_CASE("engine: solve.reset throws away a finished solve as a step", "[engine]") {
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
  // nothing to reset: nothing changes
  const std::uint64_t rev = h.result("document.get")["revision"];
  CHECK(h.result("solve.reset", {{"problem", p}})["revision"] == rev);
  h.result("solve.start", {{"problem", p}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  h.engine.poll();
  CHECK(h.result("problem.get", {{"problem", p}})["state"] == "solved");
  h.result("solve.reset", {{"problem", p}});
  Json after = h.result("problem.get", {{"problem", p}});
  CHECK(after["state"] == "unsolved");
  CHECK(after["saved"] == 0);
  CHECK(after["solve"] == "idle");
  h.result("edit.undo");
  after = h.result("problem.get", {{"problem", p}});
  CHECK(after["state"] == "solved");
  CHECK(after["saved"] == 1);
  // a paused solve is solve.abort's
  h.result("solve.prepare", {{"problem", p}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  h.engine.poll();
  CHECK(h.errc("solve.reset", {{"problem", p}}) == "invalid_state");
  CHECK(h.result("problem.get", {{"problem", p}})["solve"] == "paused");
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

// Opens an assembly explorer and polls until its explore.ready, which is
// returned with the reply's `pieces` (the events of other work that come
// meanwhile are dropped).
Json openExplorer(Harness& h, const Json& params) {
  const Json opened = h.result("explore.openAssembly", params);
  Json ready;
  for (int i = 0; i < 120000 && ready.is_null(); i++) {
    h.out.str("");
    h.engine.poll();
    for (Json& m : parse(h.out.str()).messages) {
      if (m["method"] == "explore.ready" && m["params"]["handle"] == opened["handle"]) {
        ready = std::move(m["params"]);
      }
    }
    if (ready.is_null()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  REQUIRE(ready.is_object());
  ready["pieces"] = opened["pieces"];
  return ready;
}

// A problem whose assembler takes long to prepare (solve_test_helpers.h's
// slowMatrixPuzzle, through the API): a box of `side` cells a side, every
// cell variable, and `hooks` pieces without symmetry. Its matrix is built
// in a moment and its reduce runs for seconds in a release build.
std::string hookBox(Harness& h, int side, int hooks) {
  const std::string box = h.result("shape.create", {{"size", {side, side, side}}})["shape"];
  h.result("shape.setBox", {{"shape", box}, {"tool", "variable"}, {"from", {0, 0, 0}}, {"to", {side - 1, side - 1, side - 1}}});
  const std::string p = h.result("problem.create")["problem"];
  h.result("problem.setResult", {{"problem", p}, {"shape", box}});
  for (int i = 0; i < hooks; i++) {
    const std::string hook = h.result("shape.create", {{"size", {3 + i, 2, 1}}})["shape"];
    h.result("shape.setBox", {{"shape", hook}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {2 + i, 0, 0}}});
    h.result("shape.setBox", {{"shape", hook}, {"tool", "fixed"}, {"from", {1, 1, 0}}, {"to", {1, 1, 0}}});
    h.result("problem.setPieceCount", {{"problem", p}, {"shape", hook}, {"min", 1}, {"max", 1}});
  }
  return p;
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

TEST_CASE("engine: a fit that ends inside a poll is told once the document is free",
          "[engine][tilted][parallel]") {
  // tray.fitPlacements freezes the document. A client that hears
  // tray.fitEnded changes the tray at once (the Placements dialog's Use), so
  // the end is told only by the poll that has taken it in.
  Harness h;
  const Json snap = h.result("document.new", {{"grid", "bricks"},
                                              {"tray", {{"size", 8}, {"over", 2}, {"down", 1}}}});
  const std::string p = snap["problems"][0]["id"];
  const app::Id id = *parseId(IdKind::Problem, p);
  // a domino on the first two whole cells side by side, twenty of them:
  // far too many ways to count in a test
  const int wide = snap["tray"]["box"][0];
  const std::string cells = snap["tray"]["cells"];
  const std::size_t at = cells.find("ww");
  REQUIRE(at != std::string::npos);
  const int x = static_cast<int>(at) % wide;
  const int y = static_cast<int>(at) / wide;
  REQUIRE(x + 1 < wide);
  const std::string domino = h.result("shape.create")["shape"];
  h.result("shape.setCells", {{"shape", domino}, {"tool", "fixed"}, {"cells", {{x, y, 0}, {x + 1, y, 0}}}});
  h.result("problem.setPieceCount", {{"problem", p}, {"shape", domino}, {"min", 20}, {"max", 20}});
  REQUIRE(h.result("tray.fitPlacements", {{"problem", p}, {"options", {{"threads", 1}}}})["total"] >= 2);
  app::Document& d = h.engine.state().document();

  // the fit is stopped and has ended while the engine polls: after the
  // poll took in what had ended, before it tells
  const auto ended = [](const Json& m) { return m["method"] == "tray.fitEnded"; };
  const std::vector<Json> inside = pollWith(h, [&] {
    REQUIRE(app::TrayEdits(d).fitPlacementsStop(id).has_value());
    for (int i = 0; i < 60000 && app::TrayEdits(d).fitPlacementsProgress(id)->active; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(app::TrayEdits(d).fitPlacementsProgress(id)->active);
  });
  INFO(Json(inside).dump());
  // not told yet: the document is still frozen, a tray change is refused
  CHECK(indexOf(inside, ended) < 0);
  CHECK(d.solving());
  CHECK(app::TrayEdits(d).fittingPlacements(id));

  // the next poll takes the end in and tells it, before its revision
  const std::vector<Json> after = pollWith(h, {});
  INFO(Json(after).dump());
  const std::ptrdiff_t told = indexOf(after, ended);
  const std::ptrdiff_t changed =
      indexOf(after, [](const Json& m) { return m["method"] == "document.changed"; });
  REQUIRE(told >= 0);
  REQUIRE(changed >= 0);
  CHECK(told < changed);
  const Json& e = after[static_cast<std::size_t>(told)]["params"];
  CHECK(e["problem"] == p);
  CHECK(e["stopped"] == true);
  CHECK(e["failure"].is_null());
  CHECK(after[static_cast<std::size_t>(changed)]["params"]["what"] == Json::array({"problems"}));
  // told, the document is free: the tray changes at once
  CHECK_FALSE(d.solving());
  const Json board = h.result("tray.placements")["boards"][1];
  h.result("tray.set", {{"tray", {{"placement", "custom"},
                                  {"shiftX", board["shiftX"]},
                                  {"shiftY", board["shiftY"]}}}});
  CHECK(h.result("tray.placements")["inUse"] == 1);
  // and it is told once
  CHECK(indexOf(pollWith(h, {}), ended) < 0);
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

// Cleanup item 18.2: solution.page keeps its sorted order from page to page
// (PageOrder) and sorts by keys worked out once a solution, where it sorted
// the list for every page by a comparison that walked both trees. What it
// must give is what it gave: the order a stable sort of the list by
// Problem::sortSolutions' comparison gives, which is worked out here as the
// method did it before.
namespace {

// The ids of a problem's saved solutions as solution.page orders them, by
// the list itself: the old sort.
std::vector<std::string> sortedByTheList(Harness& h, const std::string& p, int by, bool descending) {
  const app::Document& d = h.engine.state().document();
  const unsigned int index = *d.problemIndex(*parseId(IdKind::Problem, p));
  return d.readProblem(index, [&](const Problem& pr) {
    std::vector<unsigned int> order(pr.getNumberOfSavedSolutions());
    for (unsigned int i = 0; i < order.size(); i++) {
      order[i] = i;
    }
    if (const SolutionLess less = solutionSortOrder(by); less != nullptr) {
      std::stable_sort(order.begin(), order.end(), [&](unsigned int a, unsigned int b) {
        const Solution& x = pr.getSavedSolution(a);
        const Solution& y = pr.getSavedSolution(b);
        return descending ? less(y, x) : less(x, y);
      });
    } else if (descending) {
      std::reverse(order.begin(), order.end());
    }
    std::vector<std::string> ids;
    for (const unsigned int i : order) {
      ids.push_back(formatId(IdKind::Solution, pr.getSavedSolution(i).getId()));
    }
    return ids;
  });
}

// The same ids from solution.page, 37 rows a page.
std::vector<std::string> sortedByPages(Harness& h, const std::string& p, const char* sortBy,
                                       bool descending) {
  std::vector<std::string> ids;
  for (std::size_t offset = 0;; offset += 37) {
    const Json page = h.result("solution.page", {{"problem", p},
                                                 {"sortBy", sortBy},
                                                 {"descending", descending},
                                                 {"offset", offset},
                                                 {"limit", 37}});
    for (const Json& row : page["rows"]) {
      ids.push_back(row["id"].get<std::string>());
    }
    if (offset + 37 >= page["total"].get<std::size_t>()) {
      return ids;
    }
  }
}

struct PageSort {
  const char* name;
  int by;
};
const PageSort kPageSorts[] = {{"list", -1}, {"assembly", 0}, {"level", 1}, {"moves", 2}, {"pieces", 3}};

// Every sort, both ways, asked twice (the second answer is the kept order's).
void checkEverySort(Harness& h, const std::string& p) {
  for (const PageSort& sort : kPageSorts) {
    for (const bool descending : {false, true}) {
      INFO(sort.name << (descending ? " descending" : ""));
      const std::vector<std::string> want = sortedByTheList(h, p, sort.by, descending);
      // (as booleans: a failure would print two lists of hundreds of ids)
      const bool first = sortedByPages(h, p, sort.name, descending) == want;
      const bool kept = sortedByPages(h, p, sort.name, descending) == want;
      CHECK(first);
      CHECK(kept);
    }
  }
}

}  // namespace

TEST_CASE("engine: solution.page gives the order a sort of the list gives", "[engine][parallel]") {
  Harness h;
  h.result("document.open", {{"path", "tests/data/puzzles/DemoPieceGenerator.xmpuzzle"}});
  const std::string p = h.result("problem.list")["problems"][0]["id"];
  const app::Id id = *parseId(IdKind::Problem, p);
  const auto settle = [&] {
    for (int i = 0; i < 120000 && h.engine.busy(); i++) {
      h.engine.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(h.engine.busy());
    h.engine.poll();
  };
  // every assembly kept, none taken apart: by level and by moves no
  // solution is below another, and the list's order stands
  h.result("solve.start", {{"problem", p}, {"options", {{"disassemble", false}, {"keepAtMost", 0}, {"threads", 2}}}});
  REQUIRE(h.engine.state().document().solveWait(id).has_value());
  settle();
  const std::vector<std::string> all = sortedByTheList(h, p, -1, false);
  REQUIRE(all.size() > 300);
  checkEverySort(h, p);

  // Some taken apart, one at a time: by level and by moves a solution
  // without a disassembly is below none and none is below it, so the
  // comparison is no ordering of the list, and what a stable sort makes of
  // it is the algorithm's own: the pages must give that all the same.
  const auto takeApart = [&](const Json& which) {
    Json params = which;
    params["problem"] = p;
    params["threads"] = 2;
    REQUIRE(h.result("solution.analyse", params)["total"].get<unsigned int>() > 0);
    settle();
  };
  for (std::size_t i = 0; i < all.size(); i += all.size() / 12) {
    takeApart({{"which", "one"}, {"solution", all[i]}});
  }
  checkEverySort(h, p);

  // All of them. An analysis changes the solutions' levels and moves
  // without changing the list (its solutionsVersion stands): the order kept
  // from before it must not be given again.
  // (the version read from the problem, not by a page: a page of another
  // sort would take the kept order's place)
  const auto version = [&] {
    const app::Document& d = h.engine.state().document();
    return d.readProblem(*d.problemIndex(id),
                         [](const Problem& pr) { return pr.solutionsVersion(); });
  };
  const std::vector<std::string> byMovesBefore = sortedByPages(h, p, "moves", false);
  const std::uint64_t versionBefore = version();
  takeApart({{"which", "all"}});
  CHECK(version() == versionBefore);
  const std::vector<std::string> byMoves = sortedByTheList(h, p, 2, false);
  const bool moved = byMoves != byMovesBefore;
  CHECK(moved);
  const bool fresh = sortedByPages(h, p, "moves", false) == byMoves;
  CHECK(fresh);
  checkEverySort(h, p);
  // The moves differ, so the sorts had work; one assembly does not come
  // apart, and stays below none.
  const Json page = h.result("solution.page", {{"problem", p}, {"sortBy", "moves"}, {"limit", 10000}});
  CHECK(page["disassembled"].get<std::size_t>() > 900);
  CHECK(page["disassembled"].get<std::size_t>() < page["total"].get<std::size_t>());
  std::set<unsigned int> moves;
  for (const Json& row : page["rows"]) {
    if (!row["moves"].is_null()) {
      moves.insert(row["moves"].get<unsigned int>());
    }
  }
  CHECK(moves.size() >= 3);
  const bool levelsSort = sortedByTheList(h, p, 1, false) != all;
  CHECK(levelsSort);
}

// The list grows under a solve: the keys of the solutions sorted before are
// kept (the revision stands), only the new ones' are worked out. The list
// moves on between a page and any look at it, so each page is held to what
// its own rows say: the moves worked out for the row never fall, and equal
// moves stand as the list has them (every solution has a disassembly here,
// so by moves the comparison is an ordering). A key kept for the wrong
// solution, or not worked out again, would put a row out of place.
//
// A longer list must be sorted after a shorter one of the same solve, four
// times at least. How many looks this thread gets at a solve is the
// machine's affair (on a busy one the solve was through after three, the
// more so since its take-aparts come a batch at a time: cleanup item 20.2),
// so the problem is solved again until the lists have grown as often.
TEST_CASE("engine: solution.page of a list that grows under a solve", "[engine][parallel]") {
  Harness h;
  h.result("document.open", {{"path", "tests/data/puzzles/DemoPieceGenerator.xmpuzzle"}});
  const std::string p = h.result("problem.list")["problems"][0]["id"];
  unsigned int grew = 0;
  std::size_t longest = 0;
  for (int solve = 0; solve < 50 && grew < 4; solve++) {
    h.result("solve.start", {{"problem", p}, {"options", {{"disassemble", true}, {"keepAtMost", 0}, {"threads", 2}}}});
    std::size_t last = 0;
    bool first = true;
    for (int round = 0; round < 600000 && h.engine.busy(); round++) {
      h.engine.poll();
      const Json page = h.result("solution.page", {{"problem", p}, {"sortBy", "moves"}, {"limit", 10000}});
      const Json& rows = page["rows"];
      bool sorted = true;
      for (std::size_t i = 1; i < rows.size(); i++) {
        const unsigned int a = rows[i - 1]["moves"];
        const unsigned int b = rows[i]["moves"];
        sorted = sorted && (a < b || (a == b && rows[i - 1]["index"].get<unsigned int>() <
                                                    rows[i]["index"].get<unsigned int>()));
      }
      REQUIRE(sorted);
      REQUIRE(rows.size() == page["total"].get<std::size_t>());
      grew += !first && rows.size() > last ? 1U : 0U;
      first = false;
      last = rows.size();
      longest = std::max(longest, last);
    }
    REQUIRE_FALSE(h.engine.busy());
    h.engine.poll();
  }
  // lists of several lengths were sorted, one after the other
  CHECK(grew >= 4);
  CHECK(longest > 300);
  checkEverySort(h, p);
}

// Cleanup item 18.1: explore.openAssembly built the matrix on the request
// thread, under the lock a solve of the problem writes with: a big
// problem's held every call, Pause and Abort among them, for minutes. It
// answers at once now, the matrix is built on a thread of the explorer's
// own, and explore.ready tells when the handle can be used.
TEST_CASE("engine: an assembly explorer is prepared beside the calls", "[engine][parallel]") {
  Harness h;
  h.result("document.new", {{"grid", "bricks"}});

  SECTION("the handle at once, its use after explore.ready") {
    const std::string p = dominoBox(h, 2, 2, 2);
    const Json opened = h.result("explore.openAssembly", {{"problem", p}});
    CHECK(opened["pieces"] == 4);
    CHECK(opened.size() == 2);
    const std::string ax = opened["handle"];
    // until the poll has told explore.ready, however soon the build ends
    CHECK(h.engine.busy());
    CHECK(h.errc("explore.step", {{"handle", ax}}) == "invalid_state");
    CHECK(h.errc("explore.placements", {{"handle", ax}, {"piece", 0}}) == "invalid_state");
    Json ready;
    (void)pollUntil(
        h, 60000,
        [&](const Json& m) {
          if (m["method"] == "explore.ready") {
            ready = m["params"];
          }
          // a build so short tells no progress
          CHECK(m["method"] != "explore.preparing");
          return ready.is_object();
        },
        [] {});
    REQUIRE(ready.is_object());
    CHECK(ready["handle"] == ax);
    CHECK(ready["ready"] == true);
    CHECK(ready["placementsSupported"].is_boolean());
    CHECK(ready["finished"] == 0);
    CHECK(ready["message"].is_null());
    CHECK_FALSE(h.engine.busy());
    CHECK(h.result("explore.step", {{"handle", ax}, {"steps", 5}})["placements"].size() == 4);
    h.result("explore.close", {{"handle", ax}});
    CHECK(h.errc("explore.close", {{"handle", ax}}) == "not_found");
  }

  SECTION("a matrix that cannot be made is told by explore.ready, and the handle is closed") {
    // a bar of three in a box of 2x2x2, with five cubes for the rest
    const std::string box = h.result("shape.create", {{"size", {2, 2, 2}}})["shape"];
    h.result("shape.setBox", {{"shape", box}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {1, 1, 1}}});
    const std::string bar = h.result("shape.create", {{"size", {3, 1, 1}}})["shape"];
    h.result("shape.setBox", {{"shape", bar}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {2, 0, 0}}});
    const std::string cube = h.result("shape.create", {{"size", {1, 1, 1}}})["shape"];
    h.result("shape.setBox", {{"shape", cube}, {"tool", "fixed"}, {"from", {0, 0, 0}}, {"to", {0, 0, 0}}});
    const std::string p = h.result("problem.create")["problem"];
    h.result("problem.setResult", {{"problem", p}, {"shape", box}});
    h.result("problem.setPieceCount", {{"problem", p}, {"shape", bar}, {"min", 1}, {"max", 1}});
    h.result("problem.setPieceCount", {{"problem", p}, {"shape", cube}, {"min", 5}, {"max", 5}});
    const Json ready = openExplorer(h, {{"problem", p}});
    CHECK(ready["ready"] == false);
    CHECK(ready["placementsSupported"] == false);
    CHECK(ready["message"] == "A piece cannot be placed in the target shape");
    const std::string ax = ready["handle"];
    CHECK(h.errc("explore.step", {{"handle", ax}}) == "not_found");
    CHECK(h.errc("explore.close", {{"handle", ax}}) == "not_found");
    CHECK_FALSE(h.engine.busy());
    // what needs no matrix is still the call's own error
    CHECK(h.errc("explore.openAssembly", {{"problem", "p999"}}) == "not_found");
  }

  // The rest on a preparation that takes long (seconds in a release
  // build, far longer in the others): the calls are answered while it
  // runs, its progress is told, and whoever gives the handle up ends it.
  // None of these waits for the build's own end: each would take as long
  // as the build if the stop did not reach it.
  const auto untilStoppedBuildsAreGone = [&h](const std::string& ax) {
    // Each build that was given up was asked to stop, and ended for it: one
    // that ran to its end would have an explorer to give. (Looked at before
    // the poll lets it go; the wait is the stop's, a moment.)
    for (const auto& build : h.engine.state().stoppedBuilds) {
      for (int i = 0; i < 120000 && build->active(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      REQUIRE_FALSE(build->active());
      const Result<app::AssemblyExplorer> ended = build->take();
      REQUIRE_FALSE(ended.has_value());
      CHECK(ended.error().code == Errc::InvalidState);
    }
    bool told = false;
    for (int i = 0; i < 120000 && !h.engine.state().stoppedBuilds.empty(); i++) {
      h.out.str("");
      h.engine.poll();
      for (const Json& m : parse(h.out.str()).messages) {
        told = told || (m["method"] == "explore.ready" && m["params"]["handle"] == ax);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(h.engine.state().stoppedBuilds.empty());
    // an explorer given up is not told ready
    CHECK_FALSE(told);
  };

  SECTION("calls are answered and the progress told while it is prepared; explore.close ends it") {
    const std::string p = hookBox(h, 20, 4);
    const std::string ax = h.result("explore.openAssembly", {{"problem", p}})["handle"];
    CHECK(h.engine.busy());
    CHECK(h.result("engine.info")["methods"].size() > 100);
    CHECK(h.result("problem.get", {{"problem", p}})["pieces"] == 4);
    Json progress;
    const std::vector<Json> events = pollUntil(
        h, 120000,
        [&](const Json& m) {
          if (m["method"] == "explore.preparing") {
            progress = m["params"];
          }
          return progress.is_object();
        },
        [&] { (void)h.result("solve.status", {{"problem", p}}); });
    REQUIRE(progress.is_object());
    CHECK(progress["handle"] == ax);
    CHECK(progress["reducing"].is_boolean());
    CHECK(progress["progress"]["placements"].get<std::uint64_t>() > 0);
    for (const Json& m : events) {
      CHECK(m["method"] != "explore.ready");
    }
    CHECK(h.errc("explore.step", {{"handle", ax}}) == "invalid_state");
    h.result("explore.close", {{"handle", ax}});
    CHECK_FALSE(h.engine.busy());
    CHECK(h.errc("explore.close", {{"handle", ax}}) == "not_found");
    CHECK(h.errc("explore.step", {{"handle", ax}}) == "not_found");
    untilStoppedBuildsAreGone(ax);
  }

  SECTION("a solve of the problem starts, runs and pauses while its explorer is prepared") {
    // the build reads its own copy: the problem's lock is free
    const std::string p = hookBox(h, 20, 4);
    const std::string ax = h.result("explore.openAssembly", {{"problem", p}})["handle"];
    h.result("solve.start", {{"problem", p}, {"options", {{"disassemble", false}, {"threads", 1}}}});
    (void)pollUntil(
        h, 60000,
        [](const Json& m) { return m["method"] == "solve.state" && m["params"]["state"] == "preparing"; },
        [] {});
    h.result("solve.pause", {{"problem", p}});
    bool paused = false;
    const std::vector<Json> events = pollUntil(
        h, 120000,
        [&](const Json& m) {
          paused = paused || (m["method"] == "solve.state" && m["params"]["state"] == "paused");
          return paused;
        },
        [] {});
    CHECK(paused);
    for (const Json& m : events) {
      CHECK(m["method"] != "explore.ready");
    }
    // the explorer is still being prepared
    CHECK(h.errc("explore.step", {{"handle", ax}}) == "invalid_state");
    h.result("explore.close", {{"handle", ax}});
    untilStoppedBuildsAreGone(ax);
  }

  SECTION("a new document ends the preparation, and so does the engine's end") {
    const std::string p = hookBox(h, 20, 4);
    const std::string ax = h.result("explore.openAssembly", {{"problem", p}})["handle"];
    h.result("document.new", {{"grid", "bricks"}});
    CHECK_FALSE(h.engine.busy());
    CHECK(h.errc("explore.step", {{"handle", ax}}) == "not_found");
    untilStoppedBuildsAreGone(ax);
    // one left building when the engine goes: its end stops it and waits
    const std::string q = hookBox(h, 20, 4);
    (void)h.result("explore.openAssembly", {{"problem", q}});
    CHECK(h.engine.busy());
  }
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
        const std::string ax = openExplorer(h, {{"problem", p}})["handle"];
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

TEST_CASE("engine: grid.neighbor numbers the sides as the meshes do", "[engine]") {
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
      const Json n = h.result("grid.neighbor", {{"size", size}, {"cell", cell}, {"face", face}})["cell"];
      REQUIRE(n.is_array());
      // the cell across the side, filled, takes exactly that side away
      CHECK(h.result("shape.setCells", {{"shape", s}, {"tool", "fixed"}, {"cells", {n}}})["changed"] == 1);
      std::vector<int> rest = sides;
      rest.erase(std::find(rest.begin(), rest.end(), face));
      CHECK(sidesOf(h, s, voxel, spheres) == rest);
      h.result("edit.undo");
    }
    CHECK(h.errc("grid.neighbor", {{"size", size}, {"cell", cell}, {"face", 99}}) == "invalid_argument");
    CHECK(h.errc("grid.neighbor", {{"size", size}, {"cell", {10, 0, 0}}, {"face", 0}}) ==
          "invalid_argument");
  }
  // across the edge of the space: none; a cell the grid does not have
  h.result("document.new", {{"grid", "bricks"}});
  CHECK(h.result("grid.neighbor", {{"size", {2, 2, 2}}, {"cell", {0, 0, 0}}, {"face", 0}})["cell"].is_null());
  CHECK(h.result("grid.neighbor", {{"size", {2, 2, 2}}, {"cell", {0, 0, 0}}, {"face", 3}})["cell"] ==
        Json::array({1, 0, 0}));
  h.result("document.new", {{"grid", "spheres"}});
  CHECK(h.errc("grid.neighbor", {{"size", {3, 3, 3}}, {"cell", {1, 0, 0}}, {"face", 0}}) ==
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
