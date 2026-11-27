// SPDX-License-Identifier: GPL-3.0-or-later
//
// shapelogic_engine for the browser: the website's demo of the app
// (site/public/demo/, `just site-demo`) runs the engine compiled with
// Emscripten, driven line by line from JavaScript in place of stdin
// (web/src/demo/wasm-transport.ts), its output and logs taken after each
// call. One thread, so the page refuses a solve or an analysis before it
// reaches the engine (web/src/demo/engine-bridge.ts).
#include "engine.h"

#include <emscripten/emscripten.h>

#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace {

struct Browser {
  std::ostringstream out;
  std::ostringstream log;
  std::unique_ptr<sl::engine::Engine> engine;
  // what sl_take and sl_log_take last took, for sl_taken_* and sl_log_*
  std::string taken;
  std::string takenLog;
};

Browser& browser() {
  static Browser b;
  return b;
}

}  // namespace

extern "C" {

/// Makes the engine (once, before anything else).
EMSCRIPTEN_KEEPALIVE void sl_start() {
  Browser& b = browser();
  if (b.engine == nullptr) {
    b.engine = std::make_unique<sl::engine::Engine>(b.out, b.log);
  }
}

/// One request line (without its newline): its reply and events go to the output.
EMSCRIPTEN_KEEPALIVE void sl_write(const char* data, std::size_t length) {
  browser().engine->handleLine(std::string_view(data, length));
}

/// The events of the background work (the stdio engine's run loop polls between lines).
EMSCRIPTEN_KEEPALIVE void sl_poll() { browser().engine->poll(); }

/// Takes the output written since the last take: sl_taken_data/size give it.
EMSCRIPTEN_KEEPALIVE void sl_take() {
  Browser& b = browser();
  b.taken = b.out.str();
  b.out.str("");
  b.out.clear();
}

EMSCRIPTEN_KEEPALIVE const char* sl_taken_data() { return browser().taken.data(); }
EMSCRIPTEN_KEEPALIVE std::size_t sl_taken_size() { return browser().taken.size(); }

/// Takes the logs (stderr) written since the last take.
EMSCRIPTEN_KEEPALIVE void sl_log_take() {
  Browser& b = browser();
  b.takenLog = b.log.str();
  b.log.str("");
  b.log.clear();
}

EMSCRIPTEN_KEEPALIVE const char* sl_log_data() { return browser().takenLog.data(); }
EMSCRIPTEN_KEEPALIVE std::size_t sl_log_size() { return browser().takenLog.size(); }
}
