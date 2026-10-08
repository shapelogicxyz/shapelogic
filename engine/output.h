// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// What the engine writes (decision 0012): one JSON message per line, and
/// binary frames (an announcement line, then the bytes) made of typed
/// arrays that a reply describes. Also base64, for bytes inside JSON.

#include "rpc.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace bt::engine {

static_assert(std::endian::native == std::endian::little,
              "frames are little-endian; a big-endian build would have to swap");

/// The engine's stdout. Only the main thread writes it.
class Output {
 public:
  explicit Output(std::ostream& out) : out_(out) {}

  /// One message on a line of its own.
  void send(const Json& message);
  /// A notification (an event).
  void notify(std::string_view method, Json params);
  /// Sends a frame: its announcement, then the bytes. Returns its number.
  std::uint64_t frame(std::string_view bytes);

 private:
  std::ostream& out_;
  std::uint64_t frames_ = 0;
};

/// Collects typed arrays into the bytes of a frame, each at a multiple of
/// four bytes, and describes them for the reply.
class FrameBuilder {
 public:
  template <class T>
  void add(std::string_view name, std::span<const T> data) {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::uint32_t> ||
                  std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint8_t>);
    const char* type = std::is_same_v<T, float>           ? "f32"
                       : std::is_same_v<T, std::uint32_t> ? "u32"
                       : std::is_same_v<T, std::int32_t>  ? "i32"
                                                          : "u8";
    arrays_[std::string(name)] = Json{{"type", type}, {"offset", bytes_.size()}, {"length", data.size()}};
    const std::size_t at = bytes_.size();
    bytes_.resize(at + data.size_bytes());
    if (!data.empty()) {
      std::memcpy(bytes_.data() + at, data.data(), data.size_bytes());
    }
    bytes_.resize((bytes_.size() + 3) & ~std::size_t{3}, '\0');
  }
  template <class T>
  void add(std::string_view name, const std::vector<T>& data) {
    add(name, std::span<const T>(data));
  }

  [[nodiscard]] const std::string& bytes() const { return bytes_; }
  /// The reply's Frame object for this frame sent as number `frame`.
  [[nodiscard]] Json describe(std::uint64_t frame) const;

 private:
  std::string bytes_;
  Json arrays_ = Json::object();
};

/// Standard base64 with padding.
[[nodiscard]] std::string base64Encode(std::string_view bytes);
/// Decodes standard base64 (padding optional); empty on bad input.
[[nodiscard]] std::optional<std::string> base64Decode(std::string_view text);

}  // namespace bt::engine
