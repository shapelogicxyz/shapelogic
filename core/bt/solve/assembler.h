// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// The assembler interface: build the placement matrix of a problem, search
/// it for assemblies, stop and continue, save and restore the position.
/// Ported from src/lib/assembler.{h,cpp} (assembler_c); the run, stop and
/// thread rules are decision 0009's.

#include "bt/base/stop.h"
#include "bt/base/thread_pool.h"
#include "bt/model/assembly.h"
#include "bt/solve/search_run.h"
#include "bt/solve/simd_bitset.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace bt {

class Problem;
class Puzzle;

/// What can go wrong while preparing or restoring an assembler
/// (assembler_c::errState).
enum class AssemblerError : std::uint8_t {
  None,
  TooManyUnits,              ///< the pieces have more units than the result
  TooFewUnits,               ///< the pieces have too few units for the result
  CannotPlace,               ///< a piece fits nowhere in the result
  CannotRestoreVersion,      ///< a saved position of another assembler version
  CannotRestoreSyntax,       ///< a saved position that cannot be read
  CannotRestoreInterrupted,  ///< a saved parallel search of older versions that cannot go on
  PuzzleUnhandable,          ///< the problem has what this assembler cannot do (ranges, ...)
};

/// The original's message for an error.
[[nodiscard]] std::string_view errorMessage(AssemblerError e) noexcept;

/// How the matrix is built (the createMatrix parameters of the original).
struct MatrixOptions {
  bool keepMirrors = false;        ///< report mirrored assemblies too
  bool keepRotations = false;      ///< report rotated assemblies too
  bool completeRotations = false;  ///< check every transformation for duplicates
  bool strictColors = false;       ///< a voxel fits only a result voxel of its own colour
};

/// How a run searches. Kept by nobody between runs: a continued search may
/// run with other options (a stopped split or vector search goes on the way
/// it ran, whatever the thread count).
struct SearchOptions {
  /// Threads to search with; 0: as many as the pool has. A search that has
  /// not started yet splits itself over threads when this is above 1.
  unsigned int threads = 1;
  /// Where the extra threads come from; without a pool everything runs on
  /// the caller's thread (the split is still made).
  ThreadPool* pool = nullptr;
  /// Use the vector search where it fits (BURRTOOLS_NO_SIMD turned it off
  /// in the original; an A/B switch for benchmarks).
  bool vectorSearch = true;
  /// The vector instructions the vector searches may use.
  SimdKernels kernels;
};

/// How a run ended. Finished only when the search itself is through.
enum class RunEnd : std::uint8_t {
  Finished,  ///< every assembly has been reported
  Stopped,   ///< stopped (by the caller's token or the callback); the next run carries on
};

/// A search position, as it goes into a puzzle file: the assembler version
/// and the text of the `<assembler>` element.
struct SavedPosition {
  std::string version;
  std::string text;
};

/// What reduce() removed from the matrix.
struct ReduceStats {
  unsigned int rows = 0;
  unsigned int columns = 0;
};

/// An assembler for one problem. Holds references to the puzzle and the
/// problem, which must outlive it and not change while it exists.
///
/// The life of an assembler: createMatrix(), optionally reduce() and
/// restorePosition(), then assemble() until it returns Finished. Every
/// assemble() call is one run (decision 0009): it polls the given token,
/// and a callback answering false stops it; the next call carries on where
/// it stopped. Progress (finished(), iterations()) can be read from any
/// thread at any time; everything else is called from one thread at a time.
class Assembler {
 public:
  virtual ~Assembler() = default;
  Assembler(const Assembler&) = delete;
  Assembler& operator=(const Assembler&) = delete;

  /// Builds the matrix: the part of the preparation that may take a while.
  virtual AssemblerError createMatrix(const MatrixOptions& options) = 0;

  /// Changes the mirror/rotation filter of a prepared assembler (one reused
  /// for another solve with other settings).
  virtual void applySolutionFilterFlags(bool keepMirrors, bool keepRotations, bool complete) {
    (void)keepMirrors;
    (void)keepRotations;
    (void)complete;
  }

  /// More about the last error, e.g. the piece that cannot be placed or the
  /// number of units too many.
  [[nodiscard]] virtual int errorParam() const { return 0; }

  /// Removes placements that can never be part of an assembly. Optional;
  /// polls `stop` and leaves a valid (partly reduced) matrix when stopped.
  virtual ReduceStats reduce(const StopToken& stop) {
    (void)stop;
    return {};
  }

  /// The piece reduce() is working on, for progress displays.
  [[nodiscard]] virtual unsigned int reducePiece() const { return 0; }

  /// Searches, handing each assembly found to `callback`, until the search
  /// is through or `stop` (or the callback) stops it.
  virtual RunEnd assemble(const AssemblyCallback& callback, const SearchOptions& options,
                          const StopToken& stop) = 0;

  /// How far the search is, 0..1 (1 only when it is through). Any thread.
  [[nodiscard]] virtual float finished() const = 0;

  /// The search steps done so far, a measure of the work. Any thread.
  [[nodiscard]] virtual std::uint64_t iterations() const = 0;

  /// The position to save: a later restorePosition() on an assembler with
  /// the same matrix continues from here.
  [[nodiscard]] virtual SavedPosition savePosition() const = 0;

  /// Goes to a saved position. Call after createMatrix() (and reduce(), if
  /// the position was saved from a reduced matrix) and before assemble().
  virtual AssemblerError restorePosition(std::string_view text, std::string_view version) = 0;

  /// True when the matrix was built with strict colour matching.
  [[nodiscard]] bool usesStrictColors() const { return strictColors_; }

  // ---- the placement browser ------------------------------------------------

  /// Whether the placement queries below are supported.
  [[nodiscard]] virtual bool piecePlacementSupported() const { return false; }
  /// The number of placements of a piece (not a shape).
  [[nodiscard]] virtual unsigned int piecePlacementCount(unsigned int piece) const {
    (void)piece;
    return 0;
  }
  /// Steps `delta` placements from `node` (0: the first placement) through
  /// the placements of `piece`; returns the node reached and writes its
  /// orientation and position.
  virtual unsigned int piecePlacement(unsigned int node, int delta, unsigned int piece,
                                      unsigned char* tran, int* x, int* y, int* z) const {
    (void)node;
    (void)delta;
    (void)piece;
    (void)tran;
    (void)x;
    (void)y;
    (void)z;
    return 0;
  }

  // ---- the assembler debugger ----------------------------------------------

  /// Runs exactly `steps` search steps, reporting nothing.
  virtual void debugStep(unsigned long steps) { (void)steps; }
  /// The assembly the search stands at (the pieces placed so far).
  [[nodiscard]] virtual Assembly currentAssembly() const = 0;

 protected:
  Assembler() = default;

  /// Set by createMatrix().
  bool strictColors_ = false;
};

/// Warms the lazily filled caches of the result and every part shape
/// (Voxel::warmCaches), so that threads only ever read them. Called before
/// a search spreads over threads.
void warmShapeCaches(const Puzzle& puzzle, const Problem& problem);

/// True when solving `problem` can begin or continue: it is unsolved, or it
/// is being solved and holds a saved position (problem_c::canStartSolving,
/// for an assembler that is not kept in the problem).
[[nodiscard]] bool canStartSolving(const Problem& problem);

/// Starts solving `problem` with a freshly prepared `assembler`, or carries
/// on from the position the problem holds (problem_c::setAssembler).
///
/// A position that cannot be restored (another version, damaged, a parallel
/// search of older versions that was interrupted) drops everything solving
/// had found, the position included: the problem is unsolved again and the
/// next solve starts from scratch. Keeping the partial results would report
/// them a second time. The error is returned so it can be shown once.
AssemblerError beginSolving(Problem& problem, Assembler& assembler);

/// Writes the assembler's position into the problem, so that saving the
/// puzzle writes the paused search.
void storePosition(Problem& problem, const Assembler& assembler);

}  // namespace bt
