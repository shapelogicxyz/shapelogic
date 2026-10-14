// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Fixed-size bit sets for the vector searches, and the switches that choose
/// which vector instructions those searches may use.
/// Ported from the bitset part of src/lib/simd_exact_cover.h; the kernel
/// switches replace the original's SHAPELOGIC_NO_SIMD / NO_AVX2 / NO_AVX512 /
/// NO_NEON environment variables (decision 0009).

#include "shapelogic/base/assert.h"

#include <cstddef>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#elif defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
#define BT_SIMD_X86 1
#elif defined(__aarch64__) || defined(__ARM_NEON)
#define BT_SIMD_NEON 1
#endif

namespace bt {

/// Which vector instruction sets the covers may use; each is used only when
/// the processor also has it. All off gives the plain (scalar) loops, which
/// compute the same answers.
struct SimdKernels {
  bool avx2 = true;    ///< x86-64 AVX2 (off also turns AVX-512 off)
  bool avx512 = true;  ///< x86-64 AVX-512F
  bool neon = true;    ///< ARM NEON

  /// Every kernel off: the scalar loops only.
  [[nodiscard]] static constexpr SimdKernels none() noexcept { return {false, false, false}; }
};

/// A bit set of N_WORDS 64-bit words (a multiple of 4: 256-bit chunks),
/// aligned for AVX loads.
template <std::size_t N_WORDS>
struct alignas(64) SimdBitset {
  static_assert(N_WORDS > 0 && (N_WORDS % 4 == 0), "N_WORDS must be a positive multiple of 4");
  static constexpr std::size_t kNumWords = N_WORDS;
  std::uint64_t words[N_WORDS];

  constexpr SimdBitset() : words{} {}

  void set(unsigned int bit) {
    BT_ASSERT(bit < N_WORDS * 64);
    words[bit >> 6] |= (1ULL << (bit & 63));
  }

  void reset(unsigned int bit) {
    BT_ASSERT(bit < N_WORDS * 64);
    words[bit >> 6] &= ~(1ULL << (bit & 63));
  }

  [[nodiscard]] bool test(unsigned int bit) const {
    BT_ASSERT(bit < N_WORDS * 64);
    return (words[bit >> 6] & (1ULL << (bit & 63))) != 0;
  }

  void clear() {
    for (std::size_t i = 0; i < N_WORDS; ++i) {
      words[i] = 0;
    }
  }

  [[nodiscard]] bool empty() const {
    std::uint64_t acc = 0;
    for (std::size_t i = 0; i < N_WORDS; ++i) {
      acc |= words[i];
    }
    return acc == 0;
  }

  /// True when every bit of `target` is set here.
  [[nodiscard]] bool containsAll(const SimdBitset& target) const {
    std::uint64_t diff = 0;
    for (std::size_t i = 0; i < N_WORDS; ++i) {
      diff |= (target.words[i] & ~words[i]);
    }
    return diff == 0;
  }

  SimdBitset operator|(const SimdBitset& other) const {
    SimdBitset res;
    for (std::size_t i = 0; i < N_WORDS; ++i) {
      res.words[i] = words[i] | other.words[i];
    }
    return res;
  }

  SimdBitset operator&(const SimdBitset& other) const {
    SimdBitset res;
    for (std::size_t i = 0; i < N_WORDS; ++i) {
      res.words[i] = words[i] & other.words[i];
    }
    return res;
  }

  SimdBitset operator^(const SimdBitset& other) const {
    SimdBitset res;
    for (std::size_t i = 0; i < N_WORDS; ++i) {
      res.words[i] = words[i] ^ other.words[i];
    }
    return res;
  }

  bool operator==(const SimdBitset& other) const {
    for (std::size_t i = 0; i < N_WORDS; ++i) {
      if (words[i] != other.words[i]) {
        return false;
      }
    }
    return true;
  }
};

using SimdBitset256 = SimdBitset<4>;
using SimdBitset512 = SimdBitset<8>;
using SimdBitset1024 = SimdBitset<16>;
using SimdBitset2048 = SimdBitset<32>;
using SimdBitset4096 = SimdBitset<64>;
using SimdBitset8192 = SimdBitset<128>;
using SimdBitset16384 = SimdBitset<256>;
using SimdBitset32768 = SimdBitset<512>;

/// True when `a` and `b` share no set bit.
template <std::size_t N_WORDS>
[[nodiscard]] inline bool isDisjointScalar(const SimdBitset<N_WORDS>& a,
                                           const SimdBitset<N_WORDS>& b) {
  for (std::size_t i = 0; i < N_WORDS; ++i) {
    if ((a.words[i] & b.words[i]) != 0) {
      return false;
    }
  }
  return true;
}

#if defined(BT_SIMD_X86)
/// isDisjointScalar with AVX2; call only when the processor has it.
template <std::size_t N_WORDS>
__attribute__((target("avx2"))) inline bool isDisjointAvx2(const SimdBitset<N_WORDS>& a,
                                                           const SimdBitset<N_WORDS>& b) {
  constexpr std::size_t kVec = N_WORDS / 4;
  for (std::size_t i = 0; i < kVec; ++i) {
    const __m256i va = _mm256_load_si256(reinterpret_cast<const __m256i*>(&a.words[i * 4]));
    const __m256i vb = _mm256_load_si256(reinterpret_cast<const __m256i*>(&b.words[i * 4]));
    if (!_mm256_testz_si256(va, vb)) {
      return false;
    }
  }
  return true;
}

/// isDisjointScalar with AVX-512F; call only when the processor has it.
template <std::size_t N_WORDS>
__attribute__((target("avx512f"))) inline bool isDisjointAvx512(const SimdBitset<N_WORDS>& a,
                                                                const SimdBitset<N_WORDS>& b) {
  if constexpr (N_WORDS >= 8) {
    constexpr std::size_t kVec = N_WORDS / 8;
    for (std::size_t i = 0; i < kVec; ++i) {
      const __m512i va = _mm512_load_si512(static_cast<const void*>(&a.words[i * 8]));
      const __m512i vb = _mm512_load_si512(static_cast<const void*>(&b.words[i * 8]));
      if (_mm512_test_epi64_mask(va, vb) != 0) {
        return false;
      }
    }
    return true;
  } else {
    return isDisjointAvx2(a, b);
  }
}

/// True when the processor has AVX2 / AVX-512F.
[[nodiscard]] inline bool cpuHasAvx2() noexcept { return __builtin_cpu_supports("avx2") != 0; }
[[nodiscard]] inline bool cpuHasAvx512() noexcept {
  return __builtin_cpu_supports("avx512f") != 0;
}
#endif

#if defined(BT_SIMD_NEON)
/// isDisjointScalar with NEON.
template <std::size_t N_WORDS>
inline bool isDisjointNeon(const SimdBitset<N_WORDS>& a, const SimdBitset<N_WORDS>& b) {
  constexpr std::size_t kVec = N_WORDS / 2;
  for (std::size_t i = 0; i < kVec; ++i) {
    const uint64x2_t va = vld1q_u64(&a.words[i * 2]);
    const uint64x2_t vb = vld1q_u64(&b.words[i * 2]);
    const uint64x2_t c = vandq_u64(va, vb);
    if ((vgetq_lane_u64(c, 0) | vgetq_lane_u64(c, 1)) != 0) {
      return false;
    }
  }
  return true;
}
#endif

/// The row filter a cover runs.
enum class SimdKernel : std::uint8_t { Scalar, Avx2, Avx512, Neon };

/// "scalar", "avx2", "avx512" or "neon".
[[nodiscard]] constexpr const char* kernelName(SimdKernel k) noexcept {
  switch (k) {
    case SimdKernel::Avx2:
      return "avx2";
    case SimdKernel::Avx512:
      return "avx512";
    case SimdKernel::Neon:
      return "neon";
    case SimdKernel::Scalar:
      break;
  }
  return "scalar";
}

/// The kernel a cover with these switches uses on this processor (the
/// original's dispatch ladder: AVX-512 when the bit set has the 8 words it
/// needs, else AVX2, else scalar; NEON on ARM).
[[nodiscard]] inline SimdKernel chooseKernel(const SimdKernels& k, bool wideEnough) noexcept {
#if defined(BT_SIMD_X86)
  if (k.avx2 && k.avx512 && wideEnough && cpuHasAvx512()) {
    return SimdKernel::Avx512;
  }
  if (k.avx2 && cpuHasAvx2()) {
    return SimdKernel::Avx2;
  }
  return SimdKernel::Scalar;
#elif defined(BT_SIMD_NEON)
  (void)wideEnough;
  return k.neon ? SimdKernel::Neon : SimdKernel::Scalar;
#else
  (void)k;
  (void)wideEnough;
  return SimdKernel::Scalar;
#endif
}

}  // namespace bt
