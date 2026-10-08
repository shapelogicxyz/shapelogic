// SPDX-License-Identifier: GPL-3.0-or-later
#include "output.h"

#include <array>

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

namespace {

constexpr std::string_view kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

std::string base64Encode(std::string_view bytes) {
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    const auto a = static_cast<unsigned char>(bytes[i]);
    const auto b = static_cast<unsigned char>(bytes[i + 1]);
    const auto c = static_cast<unsigned char>(bytes[i + 2]);
    out += kAlphabet[a >> 2];
    out += kAlphabet[((a & 3U) << 4) | (b >> 4)];
    out += kAlphabet[((b & 15U) << 2) | (c >> 6)];
    out += kAlphabet[c & 63U];
  }
  if (i < bytes.size()) {
    const auto a = static_cast<unsigned char>(bytes[i]);
    const unsigned int b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0U;
    out += kAlphabet[a >> 2];
    out += kAlphabet[((a & 3U) << 4) | (b >> 4)];
    out += i + 1 < bytes.size() ? kAlphabet[(b & 15U) << 2] : '=';
    out += '=';
  }
  return out;
}

std::optional<std::string> base64Decode(std::string_view text) {
  std::array<int, 256> value{};
  value.fill(-1);
  for (std::size_t k = 0; k < kAlphabet.size(); k++) {
    value[static_cast<unsigned char>(kAlphabet[k])] = static_cast<int>(k);
  }
  while (!text.empty() && text.back() == '=') {
    text.remove_suffix(1);
  }
  if (text.size() % 4 == 1) {
    return std::nullopt;
  }
  std::string out;
  out.reserve(text.size() * 3 / 4);
  unsigned int acc = 0;
  int bits = 0;
  for (const char ch : text) {
    const int v = value[static_cast<unsigned char>(ch)];
    if (v < 0) {
      return std::nullopt;
    }
    acc = (acc << 6) | static_cast<unsigned int>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((acc >> bits) & 0xFFU);
    }
  }
  return out;
}

}  // namespace bt::engine
