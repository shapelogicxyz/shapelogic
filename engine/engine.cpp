// SPDX-License-Identifier: GPL-3.0-or-later
// The dispatcher and the run loop of bt-engine (decision 0012).
#include "engine.h"

#include "bt/base/assert.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace bt::engine {

// ---- state ------------------------------------------------------------------------------

State::State(Output& output, std::ostream& logs, const MethodTable& table)
    : out(output), log(logs), methods(table), doc(std::in_place, GridKind::Bricks) {}

void State::replaceDocument(app::Document d, std::optional<std::filesystem::path> from) {
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

Json State::sendFrame(const FrameBuilder& frame) { return frame.describe(out.frame(frame.bytes())); }

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
  Json params;
};

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
    r.params = *params;
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
  const Json message = Json::parse(line, nullptr, false);
  if (message.is_discarded()) {
    out_.send(errorReply(nullptr, {kParseErrorCode, "parse_error", "the line is not JSON"}));
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
    try {
      Params params(request->params);
      reply = it->second(state_, params);
    } catch (const InternalError& e) {
      reply = std::unexpected(RpcError{kInternalCode, "internal", e.what()});
    } catch (const std::exception& e) {
      reply = std::unexpected(RpcError{kInternalCode, "internal", e.what()});
    }
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
  announceChange(state_);
}

void Engine::poll() { pollEvents(state_, std::chrono::steady_clock::now()); }

bool Engine::busy() const { return state_.document().solving(); }

// ---- the run loop -----------------------------------------------------------------------

namespace {

// The lines the reader thread read, for the main thread. Shared with the
// reader, which may still be blocked on stdin when the engine ends.
class LineQueue {
 public:
  void push(std::string line) {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      lines_.push_back(std::move(line));
    }
    cv_.notify_one();
  }
  void close() {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_one();
  }
  // Waits up to `wait` for the reader to be through; whether it is.
  bool waitClosed(std::chrono::milliseconds wait) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, wait, [this] { return closed_; });
  }
  // A line, or none after `wait`; `ended` when the input is through.
  std::optional<std::string> pop(std::chrono::milliseconds wait, bool& ended) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, wait, [this] { return !lines_.empty() || closed_; });
    if (lines_.empty()) {
      ended = closed_;
      return std::nullopt;
    }
    std::string line = std::move(lines_.front());
    lines_.pop_front();
    return line;
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::string> lines_;
  bool closed_ = false;
};

}  // namespace

bool Engine::run(std::istream& in) {
  auto queue = std::make_shared<LineQueue>();
  std::thread reader([queue, &in] {
    std::string line;
    while (std::getline(in, line)) {
      queue->push(std::move(line));
    }
    queue->close();
  });
  bool ended = false;
  while (!state_.shutdown && !ended) {
    const std::chrono::milliseconds wait(busy() ? 50 : 500);
    if (std::optional<std::string> line = queue->pop(wait, ended)) {
      handleLine(*line);
    }
    poll();
  }
  if (ended || queue->waitClosed(std::chrono::milliseconds(100))) {
    reader.join();
  } else {
    // still blocked on its input (stdin): the process ends without it
    reader.detach();
  }
  return ended;
}

}  // namespace bt::engine
