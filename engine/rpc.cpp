// SPDX-License-Identifier: GPL-3.0-or-later
#include "rpc.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace bt::engine {

RpcError toRpc(const Error& e) {
  switch (e.code) {
    case Errc::InvalidArgument: return {kInvalidArgumentCode, "invalid_argument", e.message};
    case Errc::NotFound: return {kNotFoundCode, "not_found", e.message};
    case Errc::ParseError: return {kFileParseCode, "parse_error", e.message};
    case Errc::IoError: return {kIoCode, "io_error", e.message};
    case Errc::Unsupported: return {kUnsupportedCode, "unsupported", e.message};
    case Errc::InvalidState: return {kInvalidStateCode, "invalid_state", e.message};
    case Errc::Internal: return {kInternalCode, "internal", e.message};
  }
  return {kInternalCode, "internal", e.message};
}

RpcError invalidParams(std::string message) {
  return {kInvalidParamsCode, "invalid_params", std::move(message)};
}

// ---- ids --------------------------------------------------------------------------------

namespace {

std::string_view prefix(IdKind kind) {
  switch (kind) {
    case IdKind::Shape: return "s";
    case IdKind::Colour: return "c";
    case IdKind::Problem: return "p";
    case IdKind::Solution: return "sol";
    case IdKind::Playback: return "pb";
    case IdKind::AssemblyExplorer: return "ax";
    case IdKind::MovementExplorer: return "mx";
    case IdKind::RodSet: return "r";
  }
  return "?";
}

std::string_view kindName(IdKind kind) {
  switch (kind) {
    case IdKind::Shape: return "a shape";
    case IdKind::Colour: return "a colour";
    case IdKind::Problem: return "a problem";
    case IdKind::Solution: return "a solution";
    case IdKind::Playback: return "a playback";
    case IdKind::AssemblyExplorer: return "an assembly explorer";
    case IdKind::MovementExplorer: return "a movement explorer";
    case IdKind::RodSet: return "a rod set";
  }
  return "an id";
}

}  // namespace

std::string formatId(IdKind kind, std::uint64_t id) {
  return std::string(prefix(kind)) + std::to_string(id);
}

std::optional<std::uint64_t> parseId(IdKind kind, std::string_view text) {
  const std::string_view p = prefix(kind);
  if (!text.starts_with(p)) {
    return std::nullopt;
  }
  const std::string_view digits = text.substr(p.size());
  if (digits.empty() || digits.front() == '0') {
    return std::nullopt;
  }
  std::uint64_t v = 0;
  const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), v);
  if (ec != std::errc{} || end != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return v;
}

std::optional<IdKind> idKindOf(std::string_view text) {
  for (const IdKind k : {IdKind::Solution, IdKind::Shape, IdKind::Colour, IdKind::Problem,
                         IdKind::Playback, IdKind::AssemblyExplorer, IdKind::MovementExplorer,
                         IdKind::RodSet}) {
    if (parseId(k, text)) {
      return k;
    }
  }
  return std::nullopt;
}

// ---- params -----------------------------------------------------------------------------

namespace {

const Json& emptyObject() {
  static const Json empty = Json::object();
  return empty;
}

std::string fieldName(std::string_view name) { return "'" + std::string(name) + "'"; }

}  // namespace

Params::Params(const Json& params) : params_(params.is_object() ? params : emptyObject()) {}

bool Params::has(std::string_view name) const { return params_.contains(name); }

const Json* Params::field(std::string_view name) {
  read_.insert(std::string(name));
  const auto it = params_.find(name);
  return it == params_.end() ? nullptr : &*it;
}

void Params::fail(std::string message) {
  if (!error_) {
    error_ = std::move(message);
  }
}

std::string Params::string(std::string_view name) {
  const Json* f = field(name);
  if (f == nullptr) {
    fail(fieldName(name) + " is missing");
    return {};
  }
  if (!f->is_string()) {
    fail(fieldName(name) + " must be a string");
    return {};
  }
  return f->get<std::string>();
}

std::int64_t Params::integer(std::string_view name, std::int64_t min, std::int64_t max) {
  const Json* f = field(name);
  if (f == nullptr) {
    fail(fieldName(name) + " is missing");
    return min;
  }
  if (!f->is_number_integer()) {
    fail(fieldName(name) + " must be an integer");
    return min;
  }
  if (f->is_number_unsigned() &&
      f->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    fail(fieldName(name) + " is too large");
    return min;
  }
  const std::int64_t v = f->get<std::int64_t>();
  if (v < min || v > max) {
    fail(fieldName(name) + " must be from " + std::to_string(min) + " to " + std::to_string(max));
    return min;
  }
  return v;
}

double Params::number(std::string_view name, double min, double max) {
  const Json* f = field(name);
  if (f == nullptr) {
    fail(fieldName(name) + " is missing");
    return min;
  }
  if (!f->is_number()) {
    fail(fieldName(name) + " must be a number");
    return min;
  }
  const double v = f->get<double>();
  if (!(v >= min && v <= max)) {
    fail(fieldName(name) + " is out of range");
    return min;
  }
  return v;
}

bool Params::boolean(std::string_view name) {
  const Json* f = field(name);
  if (f == nullptr) {
    fail(fieldName(name) + " is missing");
    return false;
  }
  if (!f->is_boolean()) {
    fail(fieldName(name) + " must be true or false");
    return false;
  }
  return f->get<bool>();
}

const Json& Params::value(std::string_view name) {
  static const Json null;
  const Json* f = field(name);
  if (f == nullptr) {
    fail(fieldName(name) + " is missing");
    return null;
  }
  return *f;
}

std::uint64_t Params::id(std::string_view name, IdKind kind) {
  const std::string text = string(name);
  if (error_) {
    return 0;
  }
  const std::optional<std::uint64_t> v = parseId(kind, text);
  if (!v) {
    fail(fieldName(name) + " must be " + std::string(kindName(kind)) + "'s id (" +
         std::string(prefix(kind)) + "1, ...), not \"" + text + "\"");
    return 0;
  }
  return *v;
}

std::array<int, 3> Params::cell(std::string_view name) {
  std::array<int, 3> c{};
  integers(name, c, "[x, y, z]");
  return c;
}

std::array<int, 2> Params::point(std::string_view name) {
  std::array<int, 2> c{};
  integers(name, c, "[x, y]");
  return c;
}

void Params::integers(std::string_view name, std::span<int> out, std::string_view shape) {
  const Json* f = field(name);
  if (f == nullptr) {
    fail(fieldName(name) + " is missing");
    return;
  }
  if (!f->is_array() || f->size() != out.size()) {
    fail(fieldName(name) + " must be " + std::string(shape));
    return;
  }
  for (std::size_t i = 0; i < out.size(); i++) {
    const Json& v = (*f)[i];
    if (!v.is_number_integer() || v.get<std::int64_t>() < std::numeric_limits<int>::min() ||
        v.get<std::int64_t>() > std::numeric_limits<int>::max()) {
      fail(fieldName(name) + " must be " + std::string(shape) + " of integers");
      std::ranges::fill(out, 0);
      return;
    }
    out[i] = v.get<int>();
  }
}

std::optional<std::string> Params::optString(std::string_view name) {
  if (!has(name)) {
    read_.insert(std::string(name));
    return std::nullopt;
  }
  return string(name);
}

std::int64_t Params::optInteger(std::string_view name, std::int64_t def, std::int64_t min,
                                std::int64_t max) {
  if (!has(name)) {
    read_.insert(std::string(name));
    return def;
  }
  return integer(name, min, max);
}

std::optional<std::int64_t> Params::maybeInteger(std::string_view name, std::int64_t min,
                                                 std::int64_t max) {
  if (!has(name)) {
    read_.insert(std::string(name));
    return std::nullopt;
  }
  return integer(name, min, max);
}

double Params::optNumber(std::string_view name, double def, double min, double max) {
  if (!has(name)) {
    read_.insert(std::string(name));
    return def;
  }
  return number(name, min, max);
}

bool Params::optBoolean(std::string_view name, bool def) {
  if (!has(name)) {
    read_.insert(std::string(name));
    return def;
  }
  return boolean(name);
}

std::optional<std::uint64_t> Params::optId(std::string_view name, IdKind kind) {
  if (!has(name)) {
    read_.insert(std::string(name));
    return std::nullopt;
  }
  return id(name, kind);
}

RpcResult<void> Params::finish() {
  if (error_) {
    return std::unexpected(invalidParams(*error_));
  }
  for (const auto& [key, v] : params_.items()) {
    if (!read_.contains(key)) {
      return std::unexpected(invalidParams("unknown parameter " + fieldName(key)));
    }
  }
  return {};
}

}  // namespace bt::engine
