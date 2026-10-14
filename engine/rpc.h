// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// JSON-RPC errors and the reading of a request's params (decision 0012):
/// the error codes, the mapping of the core's Errc to them, and Params,
/// which reads the fields a method takes and refuses missing, mistyped and
/// unknown ones as the schema does.

#include "shapelogic/app/document.h"
#include "shapelogic/base/error.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bt::engine {

/// The engine's JSON: objects keep their keys in the order they were
/// made, so the messages read as the schema lists them.
using Json = nlohmann::ordered_json;

/// The error codes (decision 0012).
enum RpcCode : int {
  kParseErrorCode = -32700,
  kInvalidRequestCode = -32600,
  kMethodNotFoundCode = -32601,
  kInvalidParamsCode = -32602,
  kInternalCode = -32603,
  kInvalidArgumentCode = -32001,
  kNotFoundCode = -32002,
  kFileParseCode = -32003,
  kIoCode = -32004,
  kUnsupportedCode = -32005,
  kInvalidStateCode = -32006,
};

/// An error reply: the code, the name clients switch on (data.errc), the
/// message.
struct RpcError {
  int code = kInternalCode;
  std::string errc;
  std::string message;
};

template <class T>
using RpcResult = std::expected<T, RpcError>;

/// What a method returns: its result, or an error.
using Reply = RpcResult<Json>;

/// The error reply for an error of the core.
[[nodiscard]] RpcError toRpc(const Error& e);
[[nodiscard]] inline RpcError toRpc(RpcError e) { return e; }
/// A parameter that does not fit the method.
[[nodiscard]] RpcError invalidParams(std::string message);
/// The same as an unexpected value, to return from a method.
[[nodiscard]] inline std::unexpected<RpcError> paramError(std::string message) {
  return std::unexpected(invalidParams(std::move(message)));
}
/// A core error as an unexpected value.
[[nodiscard]] inline std::unexpected<RpcError> coreError(Errc code, std::string message) {
  return std::unexpected(toRpc(Error{code, std::move(message)}));
}

#define BTE_CAT_(a, b) a##b
#define BTE_CAT(a, b) BTE_CAT_(a, b)

/// In a method: evaluates a Result<T> or RpcResult<T>; on an error returns
/// it as the reply, else declares `decl` with the value.
#define BTE_TRY(decl, expr)                                                               \
  auto&& BTE_CAT(bteResult, __LINE__) = (expr);                                           \
  if (!BTE_CAT(bteResult, __LINE__)) {                                                    \
    return std::unexpected(::bt::engine::toRpc(BTE_CAT(bteResult, __LINE__).error()));   \
  }                                                                                       \
  decl = std::move(*BTE_CAT(bteResult, __LINE__))

/// The same for a Result<void> or RpcResult<void>.
#define BTE_CHECK(expr)                                                                   \
  do {                                                                                    \
    auto&& BTE_CAT(bteResult, __LINE__) = (expr);                                         \
    if (!BTE_CAT(bteResult, __LINE__)) {                                                  \
      return std::unexpected(::bt::engine::toRpc(BTE_CAT(bteResult, __LINE__).error())); \
    }                                                                                     \
  } while (false)

/// The kinds of id the API names things by, each with its prefix.
enum class IdKind : std::uint8_t {
  Shape,             ///< s7
  Colour,            ///< c2
  Problem,           ///< p1
  Solution,          ///< sol42
  Playback,          ///< pb1
  AssemblyExplorer,  ///< ax1
  MovementExplorer,  ///< mx1
  RodSet,            ///< r1, a stacking puzzle's board
};

/// "s7" for a shape with id 7.
[[nodiscard]] std::string formatId(IdKind kind, std::uint64_t id);
/// The number of an id of this kind; empty when it is not one.
[[nodiscard]] std::optional<std::uint64_t> parseId(IdKind kind, std::string_view text);
/// The kind an id is of, by its prefix; empty for none.
[[nodiscard]] std::optional<IdKind> idKindOf(std::string_view text);

/// Reads the params of a request. Each read checks the field's type (and
/// range); the first problem is kept and the reads after it give default
/// values, so a method reads everything, then calls finish(), which also
/// refuses fields the method did not read.
class Params {
 public:
  /// The params object (null: none); it must outlive the reader.
  explicit Params(const Json& params);
  Params(Json&&) = delete;

  [[nodiscard]] bool has(std::string_view name) const;

  // required fields
  std::string string(std::string_view name);
  std::int64_t integer(std::string_view name, std::int64_t min, std::int64_t max);
  unsigned int count(std::string_view name, unsigned int max = 0xFFFFFFFF) {
    return static_cast<unsigned int>(integer(name, 0, max));
  }
  double number(std::string_view name, double min, double max);
  bool boolean(std::string_view name);
  /// Any value, checked by the caller (report problems with fail()).
  const Json& value(std::string_view name);
  std::uint64_t id(std::string_view name, IdKind kind);
  /// One of a set of names (an enum of the schema).
  template <class E>
  E choice(std::string_view name, const std::vector<std::pair<std::string_view, E>>& names) {
    return pick(name, names, string(name));
  }
  /// [x, y, z].
  std::array<int, 3> cell(std::string_view name);
  /// [x, y].
  std::array<int, 2> point(std::string_view name);

  // optional fields: the default when absent
  std::optional<std::string> optString(std::string_view name);
  std::int64_t optInteger(std::string_view name, std::int64_t def, std::int64_t min,
                          std::int64_t max);
  std::optional<std::int64_t> maybeInteger(std::string_view name, std::int64_t min,
                                           std::int64_t max);
  double optNumber(std::string_view name, double def, double min, double max);
  bool optBoolean(std::string_view name, bool def);
  std::optional<std::uint64_t> optId(std::string_view name, IdKind kind);
  template <class E>
  std::optional<E> optChoice(std::string_view name,
                             const std::vector<std::pair<std::string_view, E>>& names) {
    if (!has(name)) {
      return std::nullopt;
    }
    return pick(name, names, string(name));
  }

  /// Records a problem with a field the caller checked.
  void fail(std::string message);
  /// The first problem, or a field that was not read.
  [[nodiscard]] RpcResult<void> finish();

 private:
  const Json* field(std::string_view name);
  /// An array of out.size() integers; `shape` names it in messages ("[x, y]").
  void integers(std::string_view name, std::span<int> out, std::string_view shape);
  template <class E>
  E pick(std::string_view name, const std::vector<std::pair<std::string_view, E>>& names,
         const std::string& v) {
    for (const auto& [n, e] : names) {
      if (n == v) {
        return e;
      }
    }
    if (!error_) {
      fail("'" + std::string(name) + "' cannot be \"" + v + "\"");
    }
    return names.front().second;
  }

  const Json& params_;
  std::set<std::string, std::less<>> read_;
  std::optional<std::string> error_;
};

}  // namespace bt::engine
