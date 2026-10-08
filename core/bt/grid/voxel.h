// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A voxel space: a 3D box of cells on one of the grids, with the editing
/// and transformation operations shared by all grids.
/// Ported from src/lib/voxel.{h,cpp} (meshes come in stage 6, XML reading
/// and writing lives in bt/io).

#include "bt/base/assert.h"
#include "bt/grid/grid_type.h"
#include "bt/grid/symmetries.h"
#include "bt/grid/types.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bt {

/// One voxel space. Abstract: each grid derives its own class, which knows
/// how cells neighbour each other and how the grid rotates. Make one with
/// GridType::makeVoxel() and copy it with clone().
///
/// A cell holds a state (kEmpty, kFilled, kVariable) and a colour (0..63),
/// packed into one VoxelType; result spaces of assemblies use the whole
/// byte as a piece number.
///
/// The space is indexed `x + sx*(y + sy*z)`. Coordinates on some grids are
/// only valid on a subset of positions (validCoordinate()).
///
/// Caches: the symmetry group and the hotspot/bounding box of every
/// transformation are computed lazily by const member functions. A Voxel is
/// therefore not safe to read from several threads at once until
/// warmCaches() has been called (stage 3 does that before sharing shapes).
class Voxel {
 public:
  /// Cell states (the low two bits of a cell).
  static constexpr int kEmpty = 0;
  static constexpr int kFilled = 1;
  static constexpr int kVariable = 2;

  /// What actionOnSpace() does to the cells it selects.
  enum class Action {
    Fixed,     ///< make the cells filled
    Variable,  ///< make the cells variable
    Decolor,   ///< remove their colour
  };

  virtual ~Voxel();
  Voxel& operator=(const Voxel&) = delete;

  /// A deep copy, of the same grid class.
  [[nodiscard]] virtual std::unique_ptr<Voxel> clone() const = 0;

  // ---- size and indexing --------------------------------------------------

  [[nodiscard]] unsigned int getX() const { return sx_; }  ///< x size
  [[nodiscard]] unsigned int getY() const { return sy_; }  ///< y size
  [[nodiscard]] unsigned int getZ() const { return sz_; }  ///< z size
  /// The number of cells, getX()*getY()*getZ().
  [[nodiscard]] unsigned int getXYZ() const { return voxels_; }

  /// The grid of this space.
  [[nodiscard]] const GridType& getGridType() const { return gt_; }

  /// The squared diagonal of the space.
  [[nodiscard]] unsigned int getDiagonal() const { return sx_ * sx_ + sy_ * sy_ + sz_ * sz_; }

  /// The largest of the three sizes.
  [[nodiscard]] unsigned int getBiggestDimension() const;

  /// The index of a cell.
  [[nodiscard]] unsigned int getIndex(unsigned int x, unsigned int y, unsigned int z) const {
    BT_ASSERT(x < sx_ && y < sy_ && z < sz_);
    return x + sx_ * (y + sy_ * z);
  }

  /// The coordinates of an index; false when the index is past the end.
  bool indexToXYZ(unsigned int index, unsigned int* x, unsigned int* y, unsigned int* z) const;

  // ---- raw cell access ----------------------------------------------------

  /// The cell value at a position.
  [[nodiscard]] VoxelType get(unsigned int x, unsigned int y, unsigned int z) const {
    return space_[getIndex(x, y, z)];
  }
  /// Like get(), but kEmpty for positions outside the space.
  [[nodiscard]] VoxelType get2(int x, int y, int z) const {
    if (x >= 0 && y >= 0 && z >= 0 && static_cast<long>(x) < static_cast<long>(sx_) &&
        static_cast<long>(y) < static_cast<long>(sy_) &&
        static_cast<long>(z) < static_cast<long>(sz_)) {
      return space_[getIndex(static_cast<unsigned int>(x), static_cast<unsigned int>(y),
                             static_cast<unsigned int>(z))];
    }
    return kEmpty;
  }
  /// The cell value at an index.
  [[nodiscard]] VoxelType get(unsigned int p) const {
    BT_ASSERT(p < voxels_);
    return space_[p];
  }

  /// Sets a cell. An emptied cell loses its goal mark, and a cell that was
  /// empty does not get an old goal mark back.
  void set(unsigned int x, unsigned int y, unsigned int z, VoxelType val) {
    set(getIndex(x, y, z), val);
  }
  /// Sets the cell at an index.
  void set(unsigned int p, VoxelType val);
  /// Sets every cell.
  void setAll(VoxelType val);

  /// The number of cells whose whole value is `val`.
  [[nodiscard]] unsigned int count(VoxelType val) const;

  /// True when a face neighbour (in the cube sense, the six directions) of
  /// cell `p` has value `val`.
  [[nodiscard]] bool neighbour(unsigned int p, VoxelType val) const;

  // ---- state and colour ---------------------------------------------------

  [[nodiscard]] int getState(unsigned int x, unsigned int y, unsigned int z) const {
    return get(x, y, z) & 0x3;
  }
  [[nodiscard]] int getState2(int x, int y, int z) const { return get2(x, y, z) & 0x3; }
  [[nodiscard]] int getState(unsigned int i) const { return get(i) & 0x3; }
  [[nodiscard]] unsigned int getColor(unsigned int x, unsigned int y, unsigned int z) const {
    return static_cast<unsigned int>(get(x, y, z) >> 2);
  }
  [[nodiscard]] unsigned int getColor2(int x, int y, int z) const {
    return static_cast<unsigned int>(get2(x, y, z) >> 2);
  }
  [[nodiscard]] unsigned int getColor(unsigned int i) const {
    return static_cast<unsigned int>(get(i) >> 2);
  }

  [[nodiscard]] bool isEmpty(unsigned int x, unsigned int y, unsigned int z) const {
    return getState(x, y, z) == kEmpty;
  }
  [[nodiscard]] bool isEmpty2(int x, int y, int z) const { return getState2(x, y, z) == kEmpty; }
  [[nodiscard]] bool isEmpty(unsigned int i) const { return getState(i) == kEmpty; }
  [[nodiscard]] bool isFilled(unsigned int x, unsigned int y, unsigned int z) const {
    return getState(x, y, z) == kFilled;
  }
  [[nodiscard]] bool isFilled2(int x, int y, int z) const { return getState2(x, y, z) == kFilled; }
  [[nodiscard]] bool isFilled(unsigned int i) const { return getState(i) == kFilled; }
  [[nodiscard]] bool isVariable(unsigned int x, unsigned int y, unsigned int z) const {
    return getState(x, y, z) == kVariable;
  }
  [[nodiscard]] bool isVariable2(int x, int y, int z) const {
    return getState2(x, y, z) == kVariable;
  }
  [[nodiscard]] bool isVariable(unsigned int i) const { return getState(i) == kVariable; }

  /// Sets the state of a cell, keeping its colour.
  void setState(unsigned int x, unsigned int y, unsigned int z, int state) {
    setState(getIndex(x, y, z), state);
  }
  void setState(unsigned int i, int state) {
    set(i, static_cast<VoxelType>((get(i) & ~0x3) | state));
  }
  /// Sets the colour of a cell (0..63), keeping its state.
  void setColor(unsigned int x, unsigned int y, unsigned int z, unsigned int color) {
    setColor(getIndex(x, y, z), color);
  }
  void setColor(unsigned int i, unsigned int color) {
    BT_ASSERT(color < 64);
    set(i, static_cast<VoxelType>((get(i) & 0x3u) | color << 2));
  }

  /// The number of cells in a state, whatever their colour.
  [[nodiscard]] unsigned int countState(int state) const;

  // ---- goal marks (sliding puzzles; kept so files round-trip) ------------

  /// Goal-piece mark of a cell (shape index + 1), or 0.
  [[nodiscard]] unsigned int getGoalPiece(unsigned int i) const {
    return i < goalPiece_.size() ? goalPiece_[i] : 0;
  }
  [[nodiscard]] unsigned int getGoalPiece(unsigned int x, unsigned int y, unsigned int z) const {
    return getGoalPiece(getIndex(x, y, z));
  }
  /// Sets a goal mark; clearing the last one drops the marks altogether.
  void setGoalPiece(unsigned int i, unsigned int pieceId);
  /// True when any cell has a goal mark.
  [[nodiscard]] bool hasGoalPieces() const;

  // ---- bounding box -------------------------------------------------------

  /// The box around the non-empty cells (all 0 for an empty space).
  [[nodiscard]] unsigned int boundX1() const { return bx1_; }
  [[nodiscard]] unsigned int boundX2() const { return bx2_; }
  [[nodiscard]] unsigned int boundY1() const { return by1_; }
  [[nodiscard]] unsigned int boundY2() const { return by2_; }
  [[nodiscard]] unsigned int boundZ1() const { return bz1_; }
  [[nodiscard]] unsigned int boundZ2() const { return bz2_; }

  /// Turns bounding-box updates off (`true`) for a batch of edits, and back
  /// on (`false`), which recalculates the box.
  void skipRecalcBoundingBox(bool skipit);

  /// The bounding box of the space after transformation `trans`; false when
  /// the transformation is not possible for this shape. Null pointers are
  /// not written.
  bool getBoundingBox(unsigned char trans, int* x1, int* y1, int* z1, int* x2 = nullptr,
                      int* y2 = nullptr, int* z2 = nullptr) const {
    const int* e = transformCacheEntry(trans);
    if (e[3] == kCacheNotDef) {
      return false;
    }
    if (x1) {
      *x1 = e[3];
    }
    if (x2) {
      *x2 = e[4];
    }
    if (y1) {
      *y1 = e[5];
    }
    if (y2) {
      *y2 = e[6];
    }
    if (z1) {
      *z1 = e[7];
    }
    if (z2) {
      *z2 = e[8];
    }
    return true;
  }

  // ---- comparison ---------------------------------------------------------

  /// Same size, same cells, same goal marks.
  bool operator==(const Voxel& op) const;

  /// Same content within the bounding boxes. Grids with a super-grid also
  /// require the same alignment. With `includeColors` false only states are
  /// compared.
  [[nodiscard]] virtual bool identicalInBB(const Voxel& op, bool includeColors = true) const;

  /// True when some transformation of `op` (mirrors only if `includeMirror`)
  /// is identicalInBB to this space. Slow.
  [[nodiscard]] bool identicalWithRots(const Voxel& op, bool includeMirror,
                                       bool includeColors) const;

  /// The mirroring transformation that turns this space into `op`, or 0 when
  /// there is none.
  [[nodiscard]] unsigned char getMirrorTransform(const Voxel& op) const;

  // ---- geometry, per grid -------------------------------------------------

  /// The neighbour number `idx` of cell (x, y, z) of kind `typ` (0 = face,
  /// 1 = edge, 2 = corner). False when there is no neighbour `idx`.
  virtual bool getNeighbor(unsigned int idx, unsigned int typ, int x, int y, int z, int* xn,
                           int* yn, int* zn) const = 0;

  /// Transforms a point around the origin. On the triangular grid this is
  /// not linear (a parity-dependent offset is folded in); use
  /// getTransformMatrix() for the geometric rotation.
  virtual void transformPoint(int* x, int* y, int* z, unsigned int trans) const = 0;

  /// The 3x3 rotation/mirror matrix of a transformation, row major, in the
  /// grid's own orthonormal frame.
  virtual void getTransformMatrix(unsigned int trans, double m[9]) const = 0;

  /// Rotates/mirrors the space in place. Returns false, leaving the space
  /// unchanged, when the transformation is not possible for this shape
  /// (only on the sphere grid).
  virtual bool transform(unsigned int nr) = 0;

  /// True for coordinates that are cells of the grid (e.g. spheres need an
  /// even x+y+z).
  [[nodiscard]] virtual bool validCoordinate(int x, int y, int z) const = 0;

  /// True when a shape placed with its origin at (x, y, z) stays aligned with
  /// the grid (the position has the same cell type as (0, 0, 0)).
  [[nodiscard]] virtual bool onGrid(int x, int y, int z) const = 0;

  // ---- editing ------------------------------------------------------------

  /// Shifts the content; cells that leave the space are lost, new cells get
  /// `filler`. The size stays.
  void translate(int dx, int dy, int dz, VoxelType filler);

  /// Shrinks the space to the bounding box (keeping super-grid alignment).
  virtual void minimizePiece();

  /// Changes the size, keeping the low corner; new cells get `filler`.
  void resize(unsigned int nsx, unsigned int nsy, unsigned int nsz, VoxelType filler);

  /// Grows and shifts the space so that (px, py, pz) is inside it, and
  /// updates the point to its new coordinates.
  virtual void resizeInclude(int& px, int& py, int& pz) = 0;

  /// Scales every cell up by `amount`; with `grid` only the outer edges are
  /// kept. Grids that cannot scale (spheres) do nothing.
  virtual void scale(unsigned int amount, bool grid = false);

  /// Whether the shape can be scaled down by `by`; with `action` it is.
  virtual bool scaleDown(unsigned char by, bool action);

  /// True when the cells selected by `inverse`/`value` form one group
  /// connected through faces (`type` 0), edges (1) or corners (2). The
  /// outside counts as one group; `outsideZ` false keeps it out of z (for
  /// flat pieces).
  [[nodiscard]] bool connected(char type, bool inverse, VoxelType value,
                               bool outsideZ = true) const;

  /// Fills the empty cells that are completely enclosed.
  void fillHoles(char type);

  /// Applies `action` to the inner cells (all face neighbours non-empty) or
  /// to the outer ones.
  void actionOnSpace(Action action, bool inside);

  /// Adds to this space the cells filled in both `va` at (xa, ya, za) and `vb`
  /// at (xb, yb, zb), growing it when needed. True when cells were added.
  bool unionintersect(const Voxel& va, int xa, int ya, int za, const Voxel& vb, int xb, int yb,
                      int zb);

  // ---- symmetry -----------------------------------------------------------

  /// The symmetry group of the space in its current orientation (cached).
  [[nodiscard]] Symmetry selfSymmetries() const {
    if (isSymmetryInvalid(symmetries_)) {
      calculateSelfSymmetries();
    }
    return symmetries_;
  }

  /// The smallest transformation that gives the same look as `trans`.
  [[nodiscard]] unsigned char normalizeTransformation(unsigned char trans) const {
    return gt_.getSymmetries().minimizeTransformation(selfSymmetries(), trans);
  }

  /// Computes the symmetry and every transformation's hotspot and bounding
  /// box now, so the const queries no longer write to the caches.
  void warmCaches() const;

  // ---- hotspot, name and other properties --------------------------------

  /// The hotspot: the point a piece is placed by, carried along by
  /// transformations.
  [[nodiscard]] int getHx() const { return hx_; }
  [[nodiscard]] int getHy() const { return hy_; }
  [[nodiscard]] int getHz() const { return hz_; }
  void setHotspot(int x, int y, int z);

  /// Puts the hotspot where it stays valid under all transformations
  /// (the origin, except on the sphere grid).
  virtual void initHotspot();

  /// True when the hotspot is where initHotspot() would put it, so that
  /// calling it would change nothing (and need not drop the caches).
  [[nodiscard]] virtual bool hasInitialHotspot() const;

  /// The hotspot after transformation `trans`; false when the transformation
  /// is not possible for this shape.
  bool getHotspot(unsigned char trans, int* x, int* y, int* z) const {
    BT_ASSERT(x && y && z);
    const int* e = transformCacheEntry(trans);
    if (e[0] == kCacheNotDef) {
      return false;
    }
    *x = e[0];
    *y = e[1];
    *z = e[2];
    return true;
  }

  [[nodiscard]] const std::string& getName() const { return name_; }
  void setName(const std::string& n) { name_ = n; }

  /// The weight the disassembler uses to decide which group of pieces moves.
  [[nodiscard]] int getWeight() const { return weight_; }
  void setWeight(int w) { weight_ = w; }

  /// Stacking disc radius, 0 when the shape is not a disc (stacking puzzles,
  /// kept so files round-trip).
  [[nodiscard]] unsigned int getDiskSize() const { return diskSize_; }
  void setDiskSize(unsigned int s) { diskSize_ = s; }

  /// The shape's own colour, 0xRRGGBB, or -1 for the colour of its number.
  [[nodiscard]] int getShapeColor() const { return shapeColor_; }
  void setShapeColor(int c) { shapeColor_ = c; }

  /// Attributes of the shape's file element that this version does not know
  /// (name, value), written back unchanged (decision 0006).
  [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& getExtraAttributes() const {
    return extraAttributes_;
  }
  void setExtraAttributes(std::vector<std::pair<std::string, std::string>> a) {
    extraAttributes_ = std::move(a);
  }

 protected:
  Voxel(unsigned int x, unsigned int y, unsigned int z, GridType gt, VoxelType init);
  Voxel(const Voxel&);

  /// Updates the bounding box (unless switched off), clears the colour and
  /// goal mark of empty cells, and drops the transformation cache.
  void recalcBoundingBox();

  /// Marks the symmetry as unknown.
  void invalidateSymmetry() { symmetries_ = kSymmetryInvalid; }

  /// Drops the cached hotspots and bounding boxes of all transformations.
  void invalidateTransformCache();

  // The space, shared with the grid classes, which rewrite it when they
  // transform or scale.
  GridType gt_;
  unsigned int sx_;
  unsigned int sy_;
  unsigned int sz_;
  unsigned int voxels_;
  std::vector<VoxelType> space_;

  unsigned int bx1_ = 0;
  unsigned int bx2_ = 0;
  unsigned int by1_ = 0;
  unsigned int by2_ = 0;
  unsigned int bz1_ = 0;
  unsigned int bz2_ = 0;
  bool doRecalc_ = true;

  int hx_ = 0;
  int hy_ = 0;
  int hz_ = 0;

  /// Goal marks per cell; empty when the shape has none.
  std::vector<unsigned char> goalPiece_;

 private:
  /// Union-find over the cells (see connected() and fillHoles()).
  void unionFind(std::vector<int>& tree, char type, bool inverse, VoxelType value,
                 bool outsideZ) const;

  /// Works out symmetries_ (selfSymmetries() keeps it).
  void calculateSelfSymmetries() const;

  /// Fills cache entry `trans` (hotspot and bounding box) if it is empty.
  void fillTransformCache(unsigned char trans) const;

  /// Cache entry `trans`, filled. Inline: the assemblers' rotation checks
  /// ask for every piece of every assembly (the cache holds an entry for
  /// each transformation of the grid, so its size bounds `trans`).
  const int* transformCacheEntry(unsigned char trans) const {
    BT_ASSERT(9 * std::size_t{trans} < bbHsCache_.size());
    const int* e = &bbHsCache_[9 * std::size_t{trans}];
    if (e[0] == kCacheUninit || e[3] == kCacheUninit) {
      fillTransformCache(trans);
    }
    return e;
  }

  /// Cache values: not worked out yet, and a transformation the shape
  /// cannot take.
  static constexpr int kCacheUninit = -30000;
  static constexpr int kCacheNotDef = -30001;

  std::string name_;
  int weight_ = 1;
  unsigned int diskSize_ = 0;
  int shapeColor_ = -1;
  std::vector<std::pair<std::string, std::string>> extraAttributes_;

  mutable Symmetry symmetries_ = kSymmetryInvalid;

  /// Per transformation: hotspot x, y, z and bounding box x1, x2, y1, y2,
  /// z1, z2; see fillTransformCache().
  mutable std::vector<int> bbHsCache_;
};

}  // namespace bt
