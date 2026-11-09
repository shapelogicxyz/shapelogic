// SPDX-License-Identifier: GPL-3.0-or-later

#include "shapelogic/solve/row_rotation_check.h"

#include "shapelogic/grid/voxel.h"
#include "shapelogic/model/problem.h"
#include "shapelogic/model/puzzle.h"

#include <algorithm>
#include <limits>

namespace bt {

namespace {

// An entry: the placement as a number in the low bits, ordered as
// Placement::operator< orders (transformation, x, y, z), and two flags.
constexpr std::uint64_t kComputed = std::uint64_t{1} << 63;
// The table cannot say: the turned placement is mirrored (its piece is
// swapped for its mirror partner, which the assembly's own check does),
// lies outside the coordinates below, or the transformation failed.
constexpr std::uint64_t kUnknown = std::uint64_t{1} << 62;
constexpr std::uint64_t kKeyMask = kUnknown - 1;

constexpr int kCoordBits = 18;
constexpr int kCoordBias = 1 << (kCoordBits - 1);

// The most entries a table may have (8 bytes each).
constexpr std::size_t kMaxEntries = std::size_t{1} << 22;

[[nodiscard]] bool coordFits(int c) { return c >= -kCoordBias && c < kCoordBias; }

[[nodiscard]] bool fits(const Placement& p) {
  return coordFits(p.xpos) && coordFits(p.ypos) && coordFits(p.zpos);
}

[[nodiscard]] std::uint64_t pack(const Placement& p) {
  const auto part = [](int c) { return static_cast<std::uint64_t>(c + kCoordBias); };
  return (std::uint64_t{p.transformation} << (3 * kCoordBits)) |
         (part(p.xpos) << (2 * kCoordBits)) | (part(p.ypos) << kCoordBits) | part(p.zpos);
}

}  // namespace

std::unique_ptr<RowRotationCheck> RowRotationCheck::make(const Puzzle& puzzle,
                                                         const Problem& problem,
                                                         const RotationFilter& filter,
                                                         std::vector<Row> rows) {
  if (filter.complete) {
    return nullptr;
  }

  // The pivot piece is compared first. Alone of its shape it is one row;
  // one of several, it is whichever of them sorts to its place.
  if (filter.pivot < problem.getNumberOfPieces()) {
    unsigned int first = 0;
    for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
      const unsigned int count = problem.getPartMaximum(part);
      if (filter.pivot < first + count) {
        if (count != 1) {
          return nullptr;
        }
        break;
      }
      first += count;
    }
  }

  for (const Row& r : rows) {
    if (!fits(r.placement)) {
      return nullptr;
    }
  }

  std::unique_ptr<RowRotationCheck> check(
      new RowRotationCheck(puzzle, problem, filter, std::move(rows)));
  if (!check->transforms_.empty() &&
      check->rows_.size() > kMaxEntries / check->transforms_.size()) {
    return nullptr;
  }
  check->images_ = std::make_unique<std::atomic<std::uint64_t>[]>(check->transforms_.size() *
                                                                 check->rows_.size());
  return check;
}

RowRotationCheck::RowRotationCheck(const Puzzle& puzzle, const Problem& problem,
                                   const RotationFilter& filter, std::vector<Row> rows)
    : puzzle_(puzzle),
      problem_(problem),
      gt_(puzzle.getGridType()),
      rotations_(gt_.getSymmetries().getNumTransformations()),
      pivot_(filter.pivot),
      pivotPart_(problem.getNumberOfParts()),
      mirror_(filter.mirror.get()),
      rows_(std::move(rows)) {
  const Symmetries& sym = gt_.getSymmetries();

  keys_.reserve(rows_.size());
  for (const Row& r : rows_) {
    keys_.push_back(pack(r.placement));
  }

  unsigned int first = 0;
  for (unsigned int part = 0; part < problem.getNumberOfParts(); part++) {
    firstPiece_.push_back(first);
    chiral_.push_back(
        sym.symmetryContainsMirror(problem.partShape(puzzle, part).selfSymmetries()) ? 0 : 1);
    if (pivot_ >= first && pivot_ < first + problem.getPartMaximum(part)) {
      pivotPart_ = part;
    }
    first += problem.getPartMaximum(part);
  }

  // as Assembly::smallerRotationExists(): the result's symmetries, the
  // mirroring ones only with the mirror pairs; the identity is never smaller
  const Symmetry s = problem.resultShape(puzzle).selfSymmetries();
  const unsigned int end = mirror_ != nullptr ? sym.getNumTransformationsMirror() : rotations_;
  for (unsigned int t = 1; t < end; t++) {
    if (sym.symmetrieContainsTransformation(s, t)) {
      transforms_.push_back(static_cast<unsigned char>(t));
    }
  }
}

std::uint64_t RowRotationCheck::entry(std::size_t j, std::uint32_t row) const {
  std::atomic<std::uint64_t>& slot = images_[j * rows_.size() + row];
  std::uint64_t e = slot.load(std::memory_order_relaxed);
  if (e == 0) {
    e = compute(j, row);
    slot.store(e, std::memory_order_relaxed);
  }
  return e;
}

// What Assembly::transform() makes of the row's placement: an assembly with
// only that piece placed is turned, so the table holds what the assembly's
// own check would compare, whatever the grid does to get there.
std::uint64_t RowRotationCheck::compute(std::size_t j, std::uint32_t row) const {
  const Row& r = rows_[row];
  const unsigned char t = transforms_[j];

  Assembly probe(gt_);
  for (unsigned int part = 0; part < problem_.getNumberOfParts(); part++) {
    unsigned int placed = 0;
    if (part == r.part) {
      probe.addRawPlacement(r.placement);
      placed = 1;
    }
    for (; placed < problem_.getPartMaximum(part); placed++) {
      probe.addNonPlacement();
    }
  }

  if (!probe.transform(t, puzzle_, problem_, t >= rotations_ ? &noMirror_ : nullptr)) {
    return kComputed | kUnknown;
  }

  const Placement& p = probe.getPlacements()[firstPiece_[r.part]];
  if (p.transformation == kUnplacedTrans || p.transformation >= rotations_ || !fits(p)) {
    return kComputed | kUnknown;
  }
  return kComputed | pack(p);
}

int RowRotationCheck::comparePart(std::size_t j, Scratch::Segment& seg, Scratch& s) const {
  std::uint64_t smallest = std::numeric_limits<std::uint64_t>::max();
  for (std::size_t i = seg.begin; i < seg.end; i++) {
    const std::uint64_t e = entry(j, s.rows[i]);
    if (e & kUnknown) {
      return 2;
    }
    const std::uint64_t key = e & kKeyMask;
    // sorted, the pieces of a part compare first by their smallest: one
    // below the part's own smallest settles it
    if (key < seg.ownMin) {
      return -1;
    }
    smallest = std::min(smallest, key);
  }

  if (smallest != seg.ownMin) {
    return 1;
  }
  if (seg.end - seg.begin == 1) {
    return 0;
  }
  return compareBeyondSmallest(j, seg, s, smallest);
}

// The smallest placements are the same (both hold the piece in the corner,
// say): the next ones in order, a few of them found by looking through the
// turned pieces again, the rest by sorting them.
int RowRotationCheck::compareBeyondSmallest(std::size_t j, Scratch::Segment& seg, Scratch& s,
                                            std::uint64_t smallest) const {
  constexpr std::size_t kBySearch = 4;

  if (!seg.ownSorted) {
    if (s.own.size() < s.rows.size()) {
      s.own.resize(s.rows.size());
    }
    for (std::size_t i = seg.begin; i < seg.end; i++) {
      s.own[i] = keys_[s.rows[i]];
    }
    std::sort(s.own.begin() + static_cast<std::ptrdiff_t>(seg.begin),
              s.own.begin() + static_cast<std::ptrdiff_t>(seg.end));
    seg.ownSorted = true;
  }
  const std::uint64_t* own = s.own.data() + seg.begin;
  const std::size_t count = seg.end - seg.begin;

  s.image.clear();
  for (std::size_t i = seg.begin; i < seg.end; i++) {
    s.image.push_back(entry(j, s.rows[i]) & kKeyMask);
  }

  // no two pieces of a part are placed alike, so the next in order is the
  // smallest above the last
  std::size_t at = 1;
  std::uint64_t last = smallest;
  for (; at < count && at <= kBySearch; at++) {
    std::uint64_t next = std::numeric_limits<std::uint64_t>::max();
    for (const std::uint64_t key : s.image) {
      if (key > last && key < next) {
        next = key;
      }
    }
    if (next != own[at]) {
      return next < own[at] ? -1 : 1;
    }
    last = next;
  }
  if (at == count) {
    return 0;
  }

  std::sort(s.image.begin(), s.image.end());
  for (; at < count; at++) {
    if (s.image[at] != own[at]) {
      return s.image[at] < own[at] ? -1 : 1;
    }
  }
  return 0;
}

void RowRotationCheck::buildWhole(Scratch& s) const {
  Assembly assembly(gt_);
  std::size_t seg = 0;
  for (unsigned int part = 0; part < problem_.getNumberOfParts(); part++) {
    unsigned int placed = 0;
    if (seg < s.segments.size() && s.segments[seg].part == part) {
      for (std::size_t i = s.segments[seg].begin; i < s.segments[seg].end; i++) {
        assembly.addRawPlacement(rows_[s.rows[i]].placement);
        placed++;
      }
      seg++;
    }
    for (; placed < problem_.getPartMaximum(part); placed++) {
      assembly.addNonPlacement();
    }
  }
  assembly.sort(problem_);
  s.whole = std::move(assembly);
  if (!s.turned) {
    s.turned.emplace(gt_);
  }
}

bool RowRotationCheck::rejects(Scratch& s) const {
  if (transforms_.empty()) {
    return false;
  }

  // The rows part by part, as the matrix has them (a part's rows next to
  // each other, the parts ascending): counted into place, which is cheaper
  // than sorting where a tray holds dozens of pieces.
  const std::size_t parts = firstPiece_.size();
  s.partEnd.assign(parts, 0);
  for (const std::uint32_t row : s.found) {
    s.partEnd[rows_[row].part]++;
  }
  s.segments.clear();
  bool anyChiral = false;
  std::size_t pivotSegment = std::numeric_limits<std::size_t>::max();
  std::size_t end = 0;
  for (unsigned int part = 0; part < parts; part++) {
    const std::size_t count = s.partEnd[part];
    if (count > 0) {
      if (part == pivotPart_) {
        pivotSegment = s.segments.size();
      }
      anyChiral = anyChiral || chiral_[part] != 0;
      s.segments.push_back(
          {end, end + count, part, std::numeric_limits<std::uint64_t>::max(), false});
    }
    // where the part's next row goes; back at its end when all are in
    s.partEnd[part] = end;
    end += count;
  }
  s.rows.resize(s.found.size());
  for (const std::uint32_t row : s.found) {
    s.rows[s.partEnd[rows_[row].part]++] = row;
  }
  for (Scratch::Segment& seg : s.segments) {
    for (std::size_t i = seg.begin; i < seg.end; i++) {
      seg.ownMin = std::min(seg.ownMin, keys_[s.rows[i]]);
    }
  }

  bool wholeBuilt = false;
  for (std::size_t j = 0; j < transforms_.size(); j++) {
    // Mirrored, a piece without mirror symmetry is swapped for its mirror
    // partner, which may move pieces of other parts: the assembly's own
    // check does that.
    int result = transforms_[j] >= rotations_ && anyChiral ? 2 : 0;

    // as Assembly::compare(): the pivot piece first, then piece by piece.
    // Pieces not placed are the same in both, and so is a part with none.
    if (result == 0 && pivotSegment < s.segments.size()) {
      result = comparePart(j, s.segments[pivotSegment], s);
    }
    for (std::size_t k = 0; result == 0 && k < s.segments.size(); k++) {
      if (k != pivotSegment) {
        result = comparePart(j, s.segments[k], s);
      }
    }

    if (result == -1) {
      return true;
    }
    if (result == 2) {
      if (!wholeBuilt) {
        buildWhole(s);
        wholeBuilt = true;
      }
      if (s.whole->smallerUnder(transforms_[j], puzzle_, problem_, pivot_, mirror_, *s.turned)) {
        return true;
      }
    }
  }

  return false;
}

}  // namespace bt
