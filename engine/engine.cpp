// SPDX-License-Identifier: GPL-3.0-or-later
// The dispatcher and the run loop of bt-engine (decision 0012).
#include "engine.h"

#include "bt/base/assert.h"
#include "bt/base/system.h"

#include <array>
#include <condition_variable>
#include <deque>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace bt::engine {

// ---- state ------------------------------------------------------------------------------

std::string Settings::defaultSlideFolder() {
  const std::string cache = userCacheDirectory();
  return cache.empty() ? std::string() : (std::filesystem::path(cache) / "sliding").string();
}

std::string Settings::defaultPanexFolder() {
  const std::string cache = userCacheDirectory();
  return cache.empty() ? std::string() : (std::filesystem::path(cache) / "panex").string();
}

State::State(Output& output, std::ostream& logs, const MethodTable& table)
    : out(output), log(logs), methods(table), doc(std::in_place, GridKind::Bricks) {}

void State::replaceDocument(app::Document d, std::optional<std::filesystem::path> from) {
  // the old document's work ends with it: tell the problems whose work the
  // events were reporting (after the reply, as events of this request)
  for (const auto& [id, w] : watches) {
    const std::string problem = formatId(IdKind::Problem, id);
    if (w.state != SolveState::Idle || w.launched) {
      queuedEvents.emplace_back("solve.state", Json{{"problem", problem}, {"state", "idle"}});
    }
    if (w.analysing) {
      const Result<app::AnalyseProgress> a = doc->analyseProgress(id);
      if (a) {
        queuedEvents.emplace_back("solution.analyseEnded",
                                  Json{{"problem", problem},
                                       {"total", a->total},
                                       {"done", a->done},
                                       {"separated", a->separated},
                                       {"stopped", true},
                                       {"failure", a->failure ? Json(*a->failure) : Json(nullptr)}});
      }
    }
  }
  // revisions go on from the old document's
  revisionBase_ = revision() + 1;
  playbacks.clear();
  assemblyExplorers.clear();
  movementExplorers.clear();
  watches.clear();
  doc.reset();
  doc.emplace(std::move(d));
  doc->setSettings(app::DocumentSettings{settings.undoDepth});
  path = std::move(from);
  announced = revision();
}

Json State::sendFrame(const FrameBuilder& frame) {
  if (inNotification) {
    throw std::runtime_error("a notification cannot receive a frame: send the request with an id");
  }
  return frame.describe(out.frame(frame.bytes()));
}

void State::flushEvents() {
  for (auto& [method, params] : queuedEvents) {
    out.notify(method, std::move(params));
  }
  queuedEvents.clear();
}

// ---- the dispatcher ---------------------------------------------------------------------

Engine::Engine(std::ostream& out, std::ostream& log)
    : out_(out), log_(log), state_(out_, log_, methods_) {
  addEngineMethods(methods_);
  addDocumentMethods(methods_);
  addShapeMethods(methods_);
  addColourMethods(methods_);
  addProblemMethods(methods_);
  addSolveMethods(methods_);
  addSolutionMethods(methods_);
  addPlaybackMethods(methods_);
  addExploreMethods(methods_);
  addSlidingMethods(methods_);
  addStackingMethods(methods_);
}

namespace {

Json errorReply(const Json& id, const RpcError& e) {
  Json error = Json::object();
  error["code"] = e.code;
  error["message"] = e.message;
  error["data"] = Json{{"errc", e.errc}};
  Json m = Json::object();
  m["jsonrpc"] = "2.0";
  m["id"] = id;
  m["error"] = std::move(error);
  return m;
}

// The envelope of a request, checked; the method and params when it is one.
struct Request {
  Json id;  // null for a notification
  bool notification = false;
  std::string method;
  const Json* params = nullptr;  // into the message, which outlives the call
};

const Json& noParams() {
  static const Json none;
  return none;
}

std::expected<Request, RpcError> readRequest(const Json& m) {
  const auto bad = [](std::string message) {
    return std::unexpected(RpcError{kInvalidRequestCode, "invalid_request", std::move(message)});
  };
  if (m.is_array()) {
    return bad("batches are not supported: send one request per line");
  }
  if (!m.is_object()) {
    return bad("a request is a JSON object");
  }
  Request r;
  const auto jsonrpc = m.find("jsonrpc");
  if (jsonrpc == m.end() || *jsonrpc != "2.0") {
    return bad("\"jsonrpc\" must be \"2.0\"");
  }
  const auto method = m.find("method");
  if (method == m.end() || !method->is_string()) {
    return bad("\"method\" must be a string");
  }
  r.method = method->get<std::string>();
  const auto id = m.find("id");
  if (id == m.end()) {
    r.notification = true;
  } else if (id->is_string() || id->is_number_integer() || id->is_null()) {
    r.id = *id;
  } else {
    return bad("\"id\" must be a string, an integer or null");
  }
  const auto params = m.find("params");
  if (params != m.end()) {
    if (!params->is_object()) {
      return std::unexpected(invalidParams("params must be an object (by name)"));
    }
    r.params = &*params;
  }
  for (const auto& [key, v] : m.items()) {
    if (key != "jsonrpc" && key != "method" && key != "id" && key != "params") {
      return bad("unknown member \"" + key + "\"");
    }
  }
  return r;
}

}  // namespace

void Engine::handleLine(std::string_view line) {
  if (line.size() > kMaxLine) {
    out_.send(errorReply(nullptr, {kInvalidRequestCode, "invalid_request", "the line is too long"}));
    return;
  }
  if (line.find_first_not_of(" \t\r") == std::string_view::npos) {
    return;
  }
  // the parser copes with any depth (it keeps its own stack); what comes
  // after it recurses, so deep lines are refused, never silently trimmed
  bool tooDeep = false;
  const Json message = Json::parse(
      line,
      [&tooDeep](int depth, Json::parse_event_t, Json&) {
        tooDeep = tooDeep || depth > kMaxDepth;
        return true;
      },
      false);
  if (message.is_discarded()) {
    out_.send(errorReply(nullptr, {kParseErrorCode, "parse_error", "the line is not JSON"}));
    return;
  }
  if (tooDeep) {
    out_.send(errorReply(nullptr, {kInvalidRequestCode, "invalid_request", "nesting too deep"}));
    return;
  }
  const std::expected<Request, RpcError> request = readRequest(message);
  if (!request) {
    // the id when there is one to answer to
    Json id = nullptr;
    if (message.is_object() && message.contains("id") &&
        (message["id"].is_string() || message["id"].is_number_integer())) {
      id = message["id"];
    }
    out_.send(errorReply(id, request.error()));
    return;
  }

  Reply reply = std::unexpected(RpcError{});
  const auto it = methods_.find(request->method);
  if (it == methods_.end()) {
    reply = std::unexpected(
        RpcError{kMethodNotFoundCode, "method_not_found", "no method \"" + request->method + "\""});
  } else {
    state_.inNotification = request->notification;
    try {
      Params params(request->params != nullptr ? *request->params : noParams());
      reply = it->second(state_, params);
    } catch (const InternalError& e) {
      reply = std::unexpected(RpcError{kInternalCode, "internal", e.what()});
    } catch (const std::exception& e) {
      reply = std::unexpected(RpcError{kInternalCode, "internal", e.what()});
    }
    state_.inNotification = false;
  }

  if (request->notification) {
    if (!reply) {
      log_ << "bt-engine: " << request->method << ": " << reply.error().message << '\n';
    }
  } else if (reply) {
    Json m = Json::object();
    m["jsonrpc"] = "2.0";
    m["id"] = request->id;
    m["result"] = std::move(*reply);
    out_.send(m);
  } else {
    out_.send(errorReply(request->id, reply.error()));
  }
  state_.flushEvents();
  announceChange(state_);
}

void Engine::poll() {
  state_.flushEvents();
  pollEvents(state_, std::chrono::steady_clock::now());
}

bool Engine::busy() const { return state_.document().solving(); }

// ---- the run loop -----------------------------------------------------------------------

namespace {

// A line of the input; one over the limit comes without its text.
struct Line {
  std::string text;
  bool tooLong = false;
};

// The lines the reader thread read, for the main thread. Bounded: the
// reader waits while the main thread is behind. Shared with the reader,
// which may still be blocked on stdin when the engine ends.
class LineQueue {
 public:
  // Waits while the queue is full; drops the line once the queue is stopped.
  void push(Line line) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] {
        return stopped_ || (lines_.size() < Engine::kMaxQueuedLines &&
                            bytes_ < Engine::kMaxQueuedBytes);
      });
      if (stopped_) {
        return;
      }
      bytes_ += line.text.size();
      lines_.push_back(std::move(line));
    }
    cv_.notify_all();
  }
  void close() {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
  }
  // The engine is through: a reader waiting on a full queue goes on.
  void stop() {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
    }
    cv_.notify_all();
  }
  // Waits up to `wait` for the reader to be through; whether it is.
  bool waitClosed(std::chrono::milliseconds wait) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, wait, [this] { return closed_; });
  }
  // A line, or none after `wait`; `ended` when the input is through.
  std::optional<Line> pop(std::chrono::milliseconds wait, bool& ended) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, wait, [this] { return !lines_.empty() || closed_; });
    if (lines_.empty()) {
      ended = closed_;
      return std::nullopt;
    }
    Line line = std::move(lines_.front());
    lines_.pop_front();
    bytes_ -= line.text.size();
    lock.unlock();
    cv_.notify_all();
    return line;
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Line> lines_;
  std::size_t bytes_ = 0;
  bool closed_ = false;
  bool stopped_ = false;
};

// Reads `in` line by line into the queue, keeping at most kMaxLine bytes of
// a line: the rest of a longer one is read and dropped, and the line is
// queued as too long.
void readLines(std::istream& in, LineQueue& queue) {
  std::array<char, 65536> buf{};
  Line line;
  bool pending = false;  // part of a line is in hand (a buffer filled before its newline)
  while (true) {
    in.getline(buf.data(), static_cast<std::streamsize>(buf.size()));
    const auto n = static_cast<std::size_t>(in.gcount());
    if (in.bad()) {
      break;
    }
    const bool full = in.fail() && !in.eof();     // the buffer filled before a newline
    const bool newline = !in.fail() && !in.eof();  // a newline came (counted, not stored)
    const std::size_t stored = newline ? n - 1 : n;
    if (!line.tooLong) {
      if (line.text.size() + stored > Engine::kMaxLine) {
        line.tooLong = true;
        std::string().swap(line.text);
      } else {
        line.text.append(buf.data(), stored);
      }
    }
    pending = pending || stored > 0;
    if (full) {
      in.clear();
      continue;
    }
    if (newline || pending) {
      queue.push(std::move(line));
      line = Line{};
      pending = false;
    }
    if (in.eof()) {
      break;
    }
  }
}

}  // namespace

bool Engine::run(std::istream& in) {
  auto queue = std::make_shared<LineQueue>();
  std::thread reader([queue, &in] {
    readLines(in, *queue);
    queue->close();
  });
  bool ended = false;
  while (!state_.shutdown && !ended) {
    const std::chrono::milliseconds wait(busy() ? 50 : 500);
    if (std::optional<Line> line = queue->pop(wait, ended)) {
      if (line->tooLong) {
        out_.send(errorReply(nullptr, {kInvalidRequestCode, "invalid_request", "the line is too long"}));
      } else {
        handleLine(line->text);
      }
    }
    poll();
  }
  queue->stop();
  if (ended || queue->waitClosed(std::chrono::milliseconds(100))) {
    reader.join();
  } else {
    // still blocked on its input (stdin): the process ends without it
    reader.detach();
  }
  return ended;
}

}  // namespace bt::engine
