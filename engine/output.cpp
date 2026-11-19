// SPDX-License-Identifier: GPL-3.0-or-later
#include "output.h"

namespace bt::engine {

void Output::send(const Json& message) {
  // dump escapes control characters, so a message never spans lines; bad
  // UTF-8 from a file is replaced rather than thrown on
  out_ << message.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
  out_.flush();
}

void Output::notify(std::string_view method, Json params) {
  Json m = Json::object();
  m["jsonrpc"] = "2.0";
  m["method"] = method;
  m["params"] = std::move(params);
  send(m);
}

std::uint64_t Output::frame(std::string_view bytes) {
  const std::uint64_t n = ++frames_;
  notify("frame", Json{{"frame", n}, {"bytes", bytes.size()}});
  out_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out_.flush();
  return n;
}

Json FrameBuilder::describe(std::uint64_t frame) const {
  return Json{{"frame", frame}, {"bytes", bytes_.size()}, {"arrays", arrays_}};
}

}  // namespace bt::engine
