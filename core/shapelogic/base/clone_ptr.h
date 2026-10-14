// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// ClonePtr<T>: an owning pointer that copies what it points to. It lets a
/// class that holds polymorphic or recursive members (a puzzle's voxel
/// spaces, a disassembly tree's branches) keep `= default` copy operations
/// instead of hand-written ones (decision 0005).

#include <concepts>
#include <memory>
#include <utility>

namespace bt {

namespace detail {
template <class T>
concept HasClone = requires(const T& t) {
  { t.clone() } -> std::convertible_to<std::unique_ptr<T>>;
};
}  // namespace detail

/// A nullable owning pointer with value semantics: copying it copies the
/// object, with T::clone() when T has one (polymorphic types) and with the
/// copy constructor otherwise.
template <class T>
class ClonePtr {
 public:
  ClonePtr() noexcept = default;
  ClonePtr(std::nullptr_t) noexcept {}  // NOLINT(google-explicit-constructor)
  explicit ClonePtr(std::unique_ptr<T> p) noexcept : p_(std::move(p)) {}

  ClonePtr(const ClonePtr& o) : p_(copyOf(o.p_.get())) {}
  ClonePtr(ClonePtr&&) noexcept = default;
  ClonePtr& operator=(const ClonePtr& o) {
    if (this != &o) {
      p_ = copyOf(o.p_.get());
    }
    return *this;
  }
  ClonePtr& operator=(ClonePtr&&) noexcept = default;
  ~ClonePtr() = default;

  [[nodiscard]] T* get() const noexcept { return p_.get(); }
  T& operator*() const noexcept { return *p_; }
  T* operator->() const noexcept { return p_.get(); }
  explicit operator bool() const noexcept { return p_ != nullptr; }

  /// Gives up ownership.
  [[nodiscard]] std::unique_ptr<T> release() noexcept { return std::move(p_); }
  void reset(std::unique_ptr<T> p = nullptr) noexcept { p_ = std::move(p); }

 private:
  static std::unique_ptr<T> copyOf(const T* t) {
    if (t == nullptr) {
      return nullptr;
    }
    if constexpr (detail::HasClone<T>) {
      return t->clone();
    } else {
      return std::make_unique<T>(*t);
    }
  }

  std::unique_ptr<T> p_;
};

}  // namespace bt
