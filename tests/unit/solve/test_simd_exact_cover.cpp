// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from test/test_simd_exact_cover.cpp of the original. The abort
// flag is a StopToken now, and the SHAPELOGIC_NO_* variables are SimdKernels.
#include "shapelogic/base/assert.h"
#include "shapelogic/base/stop.h"
#include "shapelogic/solve/simd_exact_cover.h"
#include "shapelogic/solve/simd_huang_cover.h"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace bt;

namespace {

using Covers = std::vector<std::vector<unsigned int>>;

// Collects the covers a solve finds, each sorted.
SimdExactCoverBase::SolutionCallback collect(Covers& out) {
  return [&out](const std::vector<unsigned int>& sol) {
    std::vector<unsigned int> sorted = sol;
    std::sort(sorted.begin(), sorted.end());
    out.push_back(sorted);
    return true;
  };
}

// Knuth's exact cover example from TAOCP: items A..G (0..6), the only cover
// is rows 1, 4, 5 ({C, E}, {A, D}, {B, G}).
template <class Solver>
void addKnuthRows(Solver& solver, unsigned int offset = 0) {
  solver.addRow(1, 0, {offset + 2, offset + 4});
  solver.addRow(2, 1, {offset + 0, offset + 3, offset + 6});
  solver.addRow(3, 2, {offset + 1, offset + 2, offset + 5});
  solver.addRow(4, 1, {offset + 0, offset + 3, offset + 5});
  solver.addRow(5, 2, {offset + 1, offset + 6});
  solver.addRow(6, 1, {offset + 3, offset + 4, offset + 6});
}

}  // namespace

TEST_CASE("SimdBitset256 operations", "[solve][simd][bitset]") {
  SimdBitset256 b;
  REQUIRE(b.empty());

  for (const unsigned int bit : {0U, 63U, 64U, 127U, 128U, 191U, 192U, 255U}) {
    b.set(bit);
  }
  for (const unsigned int bit : {0U, 63U, 64U, 127U, 128U, 191U, 192U, 255U}) {
    REQUIRE(b.test(bit));
  }
  for (const unsigned int bit : {1U, 62U, 100U, 200U}) {
    REQUIRE_FALSE(b.test(bit));
  }

  SimdBitset256 target;
  target.set(0);
  target.set(128);
  REQUIRE(b.containsAll(target));

  target.set(1);
  REQUIRE_FALSE(b.containsAll(target));

  SimdBitset256 disjointSet;
  disjointSet.set(1);
  disjointSet.set(2);
  REQUIRE(isDisjointScalar(b, disjointSet));

#if defined(BT_SIMD_X86)
  if (cpuHasAvx2()) {
    REQUIRE(isDisjointAvx2(b, disjointSet));
    REQUIRE_FALSE(isDisjointAvx2(b, target));
  }
#elif defined(BT_SIMD_NEON)
  REQUIRE(isDisjointNeon(b, disjointSet));
  REQUIRE_FALSE(isDisjointNeon(b, target));
#endif

  b.reset(0);
  CHECK_FALSE(b.test(0));
  b.clear();
  CHECK(b.empty());
}

TEST_CASE("SimdBitset512 operations", "[solve][simd][bitset]") {
  SimdBitset512 b;
  REQUIRE(b.empty());

  const unsigned int bits[] = {0, 63, 64, 127, 128, 191, 192, 255, 256, 383, 384, 511};
  for (const unsigned int bit : bits) {
    b.set(bit);
  }
  for (const unsigned int bit : bits) {
    REQUIRE(b.test(bit));
  }
  for (const unsigned int bit : {1U, 62U, 257U, 500U}) {
    REQUIRE_FALSE(b.test(bit));
  }

  SimdBitset512 target;
  target.set(0);
  target.set(384);
  REQUIRE(b.containsAll(target));

  target.set(1);
  REQUIRE_FALSE(b.containsAll(target));

  SimdBitset512 disjointSet;
  disjointSet.set(1);
  disjointSet.set(300);
  REQUIRE(isDisjointScalar(b, disjointSet));

#if defined(BT_SIMD_X86)
  if (cpuHasAvx2()) {
    REQUIRE(isDisjointAvx2(b, disjointSet));
    REQUIRE_FALSE(isDisjointAvx2(b, target));
  }
#elif defined(BT_SIMD_NEON)
  REQUIRE(isDisjointNeon(b, disjointSet));
  REQUIRE_FALSE(isDisjointNeon(b, target));
#endif
}

TEMPLATE_TEST_CASE("SimdBitset large sizes", "[solve][simd][bitset]", SimdBitset1024,
                   SimdBitset2048, SimdBitset8192, SimdBitset32768) {
  const unsigned int n = static_cast<unsigned int>(TestType::kNumWords * 64);
  TestType b;
  REQUIRE(b.empty());

  b.set(0);
  b.set(n / 2 - 1);
  b.set(n / 2);
  b.set(n - 1);

  REQUIRE(b.test(0));
  REQUIRE(b.test(n / 2 - 1));
  REQUIRE(b.test(n / 2));
  REQUIRE(b.test(n - 1));

  REQUIRE_FALSE(b.test(1));
  REQUIRE_FALSE(b.test(n / 2 - 24));
  REQUIRE_FALSE(b.test(n - 24));

  TestType target;
  target.set(0);
  target.set(n - 1);
  REQUIRE(b.containsAll(target));

  target.set(1);
  REQUIRE_FALSE(b.containsAll(target));

  TestType disjointSet;
  disjointSet.set(1);
  disjointSet.set(n - 20);
  REQUIRE(isDisjointScalar(b, disjointSet));

#if defined(BT_SIMD_X86)
  if (cpuHasAvx2()) {
    REQUIRE(isDisjointAvx2(b, disjointSet));
    REQUIRE_FALSE(isDisjointAvx2(b, target));
  }
  if (cpuHasAvx512()) {
    REQUIRE(isDisjointAvx512(b, disjointSet));
    REQUIRE_FALSE(isDisjointAvx512(b, target));
  }
#elif defined(BT_SIMD_NEON)
  REQUIRE(isDisjointNeon(b, disjointSet));
  REQUIRE_FALSE(isDisjointNeon(b, target));
#endif
}

TEST_CASE("SimdExactCover extended solve up to 32768", "[solve][simd][exact_cover]") {
  struct TierSpec {
    unsigned int capacity;
    std::function<std::unique_ptr<SimdExactCoverBase>(unsigned int, unsigned int)> make;
  };

  const std::vector<TierSpec> tiers = {
      {256, [](unsigned int c, unsigned int p) { return std::make_unique<SimdExactCover256>(c, p); }},
      {512, [](unsigned int c, unsigned int p) { return std::make_unique<SimdExactCover512>(c, p); }},
      {1024, [](unsigned int c, unsigned int p) { return std::make_unique<SimdExactCover1024>(c, p); }},
      {2048, [](unsigned int c, unsigned int p) { return std::make_unique<SimdExactCover2048>(c, p); }},
      {4096, [](unsigned int c, unsigned int p) { return std::make_unique<SimdExactCover4096>(c, p); }},
      {8192, [](unsigned int c, unsigned int p) { return std::make_unique<SimdExactCover8192>(c, p); }},
      {16384,
       [](unsigned int c, unsigned int p) { return std::make_unique<SimdExactCover16384>(c, p); }},
      {32768,
       [](unsigned int c, unsigned int p) { return std::make_unique<SimdExactCover32768>(c, p); }},
  };

  for (const auto& tier : tiers) {
    const unsigned int capacity = tier.capacity;
    const unsigned int offset = capacity - 7;
    auto solver = tier.make(capacity, 3);

    // col == capacity is out of bounds
    CHECK_THROWS_AS(solver->setRequiredColumn(capacity), InternalError);
    CHECK_THROWS_AS(solver->addRow(99, 0, {capacity}), InternalError);

    for (unsigned int i = 0; i < 7; i++) {
      solver->setRequiredColumn(offset + i);
    }
    addKnuthRows(*solver, offset);

    Covers solutions;
    std::atomic<std::uint64_t> iterations{0};
    solver->solve(collect(solutions), StopToken{}, iterations);

    REQUIRE(solutions.size() == 1);
    REQUIRE(solutions[0] == std::vector<unsigned int>{1, 4, 5});
    REQUIRE(iterations.load() > 0);

    // the factory picks the smallest size that holds the columns
    auto made = makeSimdExactCover(capacity, 3, SimdKernels{});
    CHECK(made->getNumColumns() == capacity);
  }
}

TEST_CASE("SimdExactCover256 Knuth textbook example", "[solve][simd][exact_cover]") {
  SimdExactCover256 solver(7, 3);

  SimdBitset256 req;
  for (unsigned int i = 0; i < 7; i++) {
    req.set(i);
  }
  solver.setRequiredColumns(req);
  addKnuthRows(solver);

  Covers solutions;
  std::atomic<std::uint64_t> iterations{0};
  solver.solve(collect(solutions), StopToken{}, iterations);

  REQUIRE(solutions.size() == 1);
  REQUIRE(solutions[0] == std::vector<unsigned int>{1, 4, 5});
  REQUIRE(iterations.load() > 0);
  CHECK(solver.getNumRows() == 6);
  CHECK(solver.getNumPieces() == 3);
}

TEST_CASE("SimdExactCover256 unsolvable problem", "[solve][simd][exact_cover]") {
  SimdExactCover256 solver(4, 2);

  SimdBitset256 req;
  for (unsigned int i = 0; i < 4; i++) {
    req.set(i);
  }
  solver.setRequiredColumns(req);

  // column 3 is never covered
  solver.addRow(1, 0, {0, 1});
  solver.addRow(2, 1, {1, 2});

  Covers solutions;
  std::atomic<std::uint64_t> iterations{0};
  solver.solve(collect(solutions), StopToken{}, iterations);

  REQUIRE(solutions.empty());
}

TEST_CASE("SimdExactCover256 a stopped token", "[solve][simd][exact_cover]") {
  SimdExactCover256 solver(3, 3);

  SimdBitset256 req;
  for (unsigned int i = 0; i < 3; i++) {
    req.set(i);
  }
  solver.setRequiredColumns(req);

  solver.addRow(1, 0, {0});
  solver.addRow(2, 1, {1});
  solver.addRow(3, 2, {2});

  const StopSource stopped;
  stopped.request(StopKind::Pause);
  std::atomic<std::uint64_t> iterations{0};
  Covers solutions;
  solver.solve(collect(solutions), stopped.token(), iterations);

  REQUIRE(solutions.empty());
}

TEST_CASE("SimdExactCover512 large column cover", "[solve][simd][exact_cover]") {
  // columns across the 256-bit boundary: 3 pieces, required {50, 150, 270,
  // 350, 400}; row 4 conflicts with row 2
  SimdExactCover512 solver(401, 3);
  SimdBitset512 req;
  for (const unsigned int c : {50U, 150U, 270U, 350U, 400U}) {
    req.set(c);
  }
  solver.setRequiredColumns(req);

  solver.addRow(1, 0, {50, 270});
  solver.addRow(2, 1, {150});
  solver.addRow(3, 2, {350, 400});
  solver.addRow(4, 0, {50, 150});

  Covers solutions;
  std::atomic<std::uint64_t> iterations{0};
  solver.solve(collect(solutions), StopToken{}, iterations);

  REQUIRE(solutions.size() == 1);
  REQUIRE(solutions[0] == std::vector<unsigned int>{1, 2, 3});
  REQUIRE(iterations.load() > 0);
}

TEST_CASE("SimdExactCover256 solveSubtree", "[solve][simd][exact_cover]") {
  SimdExactCover256 solver(7, 3);

  SimdBitset256 req;
  for (unsigned int i = 0; i < 7; i++) {
    req.set(i);
  }
  solver.setRequiredColumns(req);
  addKnuthRows(solver);

  Covers solutions;
  std::atomic<std::uint64_t> iterations{0};

  // a subtree with the right prefix finds the cover
  solver.solveSubtree({1}, collect(solutions), StopToken{}, iterations);
  REQUIRE(solutions.size() == 1);
  REQUIRE(solutions[0] == std::vector<unsigned int>{1, 4, 5});

  // one with a wrong prefix finds nothing
  solutions.clear();
  solver.solveSubtree({2}, collect(solutions), StopToken{}, iterations);
  REQUIRE(solutions.empty());

  // a node that names no row, and rows that conflict: nothing, no crash
  solver.solveSubtree({99}, collect(solutions), StopToken{}, iterations);
  solver.solveSubtree({2, 4}, collect(solutions), StopToken{}, iterations);
  REQUIRE(solutions.empty());
}

TEST_CASE("SimdExactCover256 concurrent solveSubtree", "[solve][simd][exact_cover][parallel]") {
  SimdExactCover256 solver(7, 3);

  SimdBitset256 req;
  for (unsigned int i = 0; i < 7; i++) {
    req.set(i);
  }
  solver.setRequiredColumns(req);
  addKnuthRows(solver);

  Covers allSolutions;
  std::mutex solMutex;
  std::atomic<std::uint64_t> iterations{0};

  // the rows covering column 0 are 2 and 4: one thread each
  std::vector<std::thread> threads;
  for (const unsigned int p : {2U, 4U}) {
    threads.emplace_back([&, p] {
      std::atomic<std::uint64_t> threadIter{0};
      solver.solveSubtree(
          {p},
          [&](const std::vector<unsigned int>& sol) {
            std::vector<unsigned int> sorted = sol;
            std::sort(sorted.begin(), sorted.end());
            const std::lock_guard<std::mutex> lock(solMutex);
            allSolutions.push_back(sorted);
            return true;
          },
          StopToken{}, threadIter);
      iterations.fetch_add(threadIter.load());
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  REQUIRE(allSolutions.size() == 1);
  REQUIRE(allSolutions[0] == std::vector<unsigned int>{1, 4, 5});
  REQUIRE(iterations.load() > 0);
}

TEST_CASE("SimdExactCover256 bounds check assertions", "[solve][simd][exact_cover]") {
  CHECK_THROWS_AS(SimdExactCover256(257, 1), InternalError);

  SimdExactCover256 solver(10, 2);
  CHECK_THROWS_AS(solver.addRow(1, 0, {10}), InternalError);
  CHECK_THROWS_AS(solver.addRow(2, 0, {256}), InternalError);

  SimdBitset256 bitset;
  CHECK_THROWS_AS(bitset.set(256), InternalError);
  CHECK_THROWS_AS(bitset.reset(256), InternalError);
  CHECK_THROWS_AS((void)bitset.test(256), InternalError);
}

TEST_CASE("SimdHuangCover256 duplicate pieces exact cover", "[solve][simd][huang]") {
  // shape 1 (col 1): 2 pieces; shape 2 (col 2): 1 piece; voxels cols 3..7
  SimdHuangCover256 solver(7, 2);

  solver.setColumnBounds(1, 2, 2, false, true, false, false);
  solver.setColumnBounds(2, 1, 1, false, true, false, false);
  for (unsigned int v = 3; v <= 7; v++) {
    solver.setColumnBounds(v, 1, 1, true, false, false, false);
  }

  // shape 1: voxels {3, 4}, {5, 6}, {6, 7}
  solver.addRow(1, 0, 1, 0, 0, {1, 3, 4}, {1, 1, 1});
  solver.addRow(2, 0, 1, 1, 0, {1, 5, 6}, {1, 1, 1});
  solver.addRow(3, 0, 1, 2, 0, {1, 6, 7}, {1, 1, 1});
  // shape 2: voxel 7, voxel 5
  solver.addRow(4, 1, 2, 0, 0, {2, 7}, {1, 1});
  solver.addRow(5, 1, 2, 1, 0, {2, 5}, {1, 1});

  Covers solutions;
  std::atomic<std::uint64_t> iterations{0};
  solver.solve(collect(solutions), StopToken{}, iterations);

  // two covers: {1, 2, 4} and {1, 3, 5}
  REQUIRE(solutions.size() == 2);
  const std::set<std::vector<unsigned int>> solSet(solutions.begin(), solutions.end());
  REQUIRE(solSet.count(std::vector<unsigned int>{1, 2, 4}) == 1);
  REQUIRE(solSet.count(std::vector<unsigned int>{1, 3, 5}) == 1);
  REQUIRE(iterations.load() > 0);
}

TEMPLATE_TEST_CASE("SimdHuangCover extended sizes duplicate pieces exact cover",
                   "[solve][simd][huang]", SimdHuangCover512, SimdHuangCover1024,
                   SimdHuangCover4096, SimdHuangCover32768) {
  const unsigned int baseV = 280;
  TestType solver(baseV + 5, 2);

  solver.setColumnBounds(1, 2, 2, false, true, false, false);
  solver.setColumnBounds(2, 1, 1, false, true, false, false);
  for (unsigned int v = baseV; v < baseV + 5; v++) {
    solver.setColumnBounds(v, 1, 1, true, false, false, false);
  }

  solver.addRow(1, 0, 1, 0, 0, {1, baseV, baseV + 1}, {1, 1, 1});
  solver.addRow(2, 0, 1, 1, 0, {1, baseV + 2, baseV + 3}, {1, 1, 1});
  solver.addRow(3, 0, 1, 2, 0, {1, baseV + 3, baseV + 4}, {1, 1, 1});
  solver.addRow(4, 1, 2, 0, 0, {2, baseV + 4}, {1, 1});
  solver.addRow(5, 1, 2, 1, 0, {2, baseV + 2}, {1, 1});

  Covers solutions;
  std::atomic<std::uint64_t> iterations{0};
  solver.solve(collect(solutions), StopToken{}, iterations);

  REQUIRE(solutions.size() == 2);
  REQUIRE(solutions[0] == std::vector<unsigned int>{1, 2, 4});
  REQUIRE(solutions[1] == std::vector<unsigned int>{1, 3, 5});
  REQUIRE(iterations.load() > 0);

  // What the threads of a split search call for a task of the plain search,
  // with the rows the task placed and the rows it hid: the covers that hold
  // the first and none of the second, each once.
  auto subtree = [&](const std::vector<unsigned int>& placed,
                     const std::vector<unsigned int>& hidden) {
    Covers found;
    std::atomic<std::uint64_t> it{0};
    solver.solveSubtree(placed, hidden, collect(found), StopToken{}, it);
    std::sort(found.begin(), found.end());
    return found;
  };

  // nothing placed, nothing hidden: the whole search
  CHECK(subtree({}, {}) == Covers{{1, 2, 4}, {1, 3, 5}});
  // a row placed (in any order with the rows the search adds)
  CHECK(subtree({2}, {}) == Covers{{1, 2, 4}});
  CHECK(subtree({5}, {}) == Covers{{1, 3, 5}});
  CHECK(subtree({1}, {}) == Covers{{1, 2, 4}, {1, 3, 5}});
  CHECK(subtree({4, 1}, {}) == Covers{{1, 2, 4}});
  // a row hidden; 0 separates groups of hidden rows and is no row
  CHECK(subtree({}, {0, 2, 0}) == Covers{{1, 3, 5}});
  CHECK(subtree({1}, {3}) == Covers{{1, 2, 4}});
  // rows that do not go together: no cover, and no crash
  CHECK(subtree({2, 3}, {}).empty());
  CHECK(subtree({1}, {2, 3}).empty());
}

// The kernel switches must reach the kernel that runs. Every kernel gives
// the same answers, so only the dispatch decision itself can show it. (In
// the original, gating only AVX2 on SHAPELOGIC_NO_AVX2 left AVX-512 on.)
TEMPLATE_TEST_CASE("SimdHuangCover: the kernel switches turn every kernel off",
                   "[solve][simd][huang][dispatch]", SimdHuangCover256, SimdHuangCover512,
                   SimdHuangCover1024, SimdHuangCover4096, SimdHuangCover32768) {
  SECTION("all off falls back to scalar") {
    const TestType solver(7, 2, SimdKernels::none());
    CHECK(solver.kernel() == SimdKernel::Scalar);
  }

  SECTION("AVX2 off does not leave AVX-512 on") {
    SimdKernels k;
    k.avx2 = false;
    const TestType solver(7, 2, k);
    CHECK(solver.kernel() != SimdKernel::Avx512);
    CHECK(solver.kernel() != SimdKernel::Avx2);
  }

  SECTION("AVX-512 off leaves the narrower kernels alone") {
    SimdKernels k;
    k.avx512 = false;
    const TestType solver(7, 2, k);
    CHECK(solver.kernel() != SimdKernel::Avx512);
  }

  SECTION("NEON off") {
    SimdKernels k;
    k.neon = false;
    const TestType solver(7, 2, k);
    CHECK(solver.kernel() != SimdKernel::Neon);
  }
}

// New: the same switches on the exact cover, and every kernel finding the
// same covers.
TEST_CASE("SimdExactCover: every kernel finds the same covers", "[solve][simd][dispatch]") {
  for (const SimdKernels k : {SimdKernels{}, SimdKernels::none()}) {
    SimdExactCover512 solver(400, 3, k);
    for (unsigned int i = 0; i < 7; i++) {
      solver.setRequiredColumn(300 + i);
    }
    addKnuthRows(solver, 300);
    Covers solutions;
    std::atomic<std::uint64_t> iterations{0};
    solver.solve(collect(solutions), StopToken{}, iterations);
    INFO(kernelName(solver.kernel()));
    CHECK(solutions == Covers{{1, 4, 5}});
  }
  CHECK(SimdExactCover256(7, 3, SimdKernels::none()).kernel() == SimdKernel::Scalar);
  CHECK(std::string(kernelName(SimdKernel::Avx2)) == "avx2");
}

// A range column must not be branched on when there are no variable voxels:
// nothing keeps its rows in order the way a shape column's are, so two range
// rows that go together were reported once for each order. The columns are
// laid out so that the range column has the fewest choices. From upstream
// burr-tools (Arne Köhn, #114).
TEMPLATE_TEST_CASE("SimdHuangCover: a range column reports each assembly once",
                   "[solve][simd][huang][range]", SimdHuangCover256, SimdHuangCover512) {
  // column 1: the shape, exactly 2 pieces; 2-5: voxels; 6: range
  TestType solver(6, 1);
  solver.setColumnBounds(1, 2, 2, false, true, false, false);
  for (unsigned int v = 2; v <= 5; v++) {
    solver.setColumnBounds(v, 1, 1, true, false, false, false);
  }
  solver.setColumnBounds(6, 1, 100, false, false, true, false);

  // rows 1 and 2 cover half the voxels each and carry range weight; rows
  // 3-6 carry none, every voxel has 3 rows and the range column 2. The only
  // assembly is {1, 2}.
  solver.addRow(1, 0, 1, 0, 1, {1, 2, 3, 6}, {1, 1, 1, 1});
  solver.addRow(2, 0, 1, 1, 1, {1, 4, 5, 6}, {1, 1, 1, 1});
  solver.addRow(3, 0, 1, 2, 0, {1, 2, 4}, {1, 1, 1});
  solver.addRow(4, 0, 1, 3, 0, {1, 3, 5}, {1, 1, 1});
  solver.addRow(5, 0, 1, 4, 0, {1, 2, 5}, {1, 1, 1});
  solver.addRow(6, 0, 1, 5, 0, {1, 3, 4}, {1, 1, 1});

  Covers sols;
  std::atomic<std::uint64_t> iterations{0};
  solver.solve(collect(sols), StopToken{}, iterations);

  REQUIRE(sols.size() == 1);
  CHECK(sols[0] == std::vector<unsigned int>{1, 2});
}

// Optional columns (assembler 0's variable voxels) and the hole budget.
// From upstream burr-tools (Arne Köhn, #115).
TEMPLATE_TEST_CASE("SimdExactCover: optional columns and the hole budget",
                   "[solve][simd][exact_cover][holes]", SimdExactCover256, SimdExactCover512) {
  // column 0: the piece; 1, 2: voxels that must be filled; 3: a variable
  // voxel, which may stay empty if the budget allows
  auto build = [](unsigned int budget, bool fillable) {
    auto solver = std::make_unique<TestType>(4, 1);
    solver->setHoleBudget(budget);
    solver->setRequiredColumn(0);
    solver->setRequiredColumn(1);
    solver->setRequiredColumn(2);
    solver->setOptionalColumn(3);
    solver->addRow(1, 0, {0, 1, 2});  // leaves the variable voxel empty
    if (fillable) {
      solver->addRow(2, 0, {0, 1, 2, 3});  // fills it
    }
    return solver;
  };

  auto run = [](TestType& solver) {
    Covers sols;
    std::atomic<std::uint64_t> iterations{0};
    solver.solve(collect(sols), StopToken{}, iterations);
    std::sort(sols.begin(), sols.end());
    return sols;
  };

  SECTION("an optional column is never a dead end and is not asked for at the goal") {
    auto sols = run(*build(1, true));
    REQUIRE(sols.size() == 2);
    CHECK(sols[0] == std::vector<unsigned int>{1});
    CHECK(sols[1] == std::vector<unsigned int>{2});
  }

  SECTION("the budget is looked at when a column is chosen, not at the goal") {
    // as in the plain search: with no hole allowed row 1 is still reported,
    // because the variable voxel could have been filled where the choice was
    // made
    CHECK(run(*build(0, true)).size() == 2);
  }

  SECTION("an optional column no row can fill uses up the budget") {
    CHECK(run(*build(0, false)).empty());
    auto sols = run(*build(1, false));
    REQUIRE(sols.size() == 1);
    CHECK(sols[0] == std::vector<unsigned int>{1});
  }

  SECTION("two rows may not share an optional column") {
    // two pieces, one voxel each must fill, both want the variable voxel
    TestType solver(5, 2);
    solver.setHoleBudget(1);
    for (unsigned int c = 0; c < 4; c++) {
      solver.setRequiredColumn(c);
    }
    solver.setOptionalColumn(4);
    solver.addRow(1, 0, {0, 2, 4});
    solver.addRow(2, 1, {1, 3, 4});
    solver.addRow(3, 1, {1, 3});
    auto sols = run(solver);
    REQUIRE(sols.size() == 1);
    CHECK(sols[0] == std::vector<unsigned int>{1, 3});
  }
}

// A search tells how far it is and counts its nodes while it runs, not
// only when it is through.
TEST_CASE("SimdExactCover: progress and iterations are reported during the search",
          "[solve][simd][exact_cover][progress]") {
  // n pieces, each can go to any of n places: n! covers
  const unsigned int n = 8;
  SimdExactCover256 solver(2 * n, n);
  for (unsigned int c = 0; c < 2 * n; c++) {
    solver.setRequiredColumn(c);
  }
  unsigned int node = 1;
  for (unsigned int p = 0; p < n; p++) {
    for (unsigned int place = 0; place < n; place++) {
      solver.addRow(node++, p, {p, n + place});
    }
  }

  std::atomic<std::uint64_t> iterations{0};
  std::uint64_t solutions = 0;
  std::uint64_t seenAtFirstSolution = 0;
  std::vector<float> fractions;
  solver.solve(
      [&](const std::vector<unsigned int>&) {
        if (solutions++ == 0) {
          seenAtFirstSolution = iterations.load();
        }
        return true;
      },
      StopToken{}, iterations, [&](float f) { fractions.push_back(f); });

  CHECK(solutions == 40320);
  CHECK(seenAtFirstSolution > 0);
  REQUIRE(fractions.size() > 4);
  CHECK(std::is_sorted(fractions.begin(), fractions.end()));
  CHECK(fractions.front() >= 0.0F);
  CHECK(fractions.back() <= 1.0F);
  CHECK(fractions.back() > 0.5F);
}

// New: a stop from the callback ends the search at once.
TEST_CASE("SimdExactCover: a stop during the search ends it", "[solve][simd][exact_cover]") {
  const unsigned int n = 6;
  SimdExactCover256 solver(2 * n, n);
  for (unsigned int c = 0; c < 2 * n; c++) {
    solver.setRequiredColumn(c);
  }
  unsigned int node = 1;
  for (unsigned int p = 0; p < n; p++) {
    for (unsigned int place = 0; place < n; place++) {
      solver.addRow(node++, p, {p, n + place});
    }
  }

  const StopSource stop;
  std::atomic<std::uint64_t> iterations{0};
  unsigned int found = 0;
  solver.solve(
      [&](const std::vector<unsigned int>&) {
        if (++found == 10) {
          stop.request(StopKind::Pause);
        }
        return found < 10;
      },
      stop.token(), iterations);
  CHECK(found == 10);
}
