// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from src/gui/tooltabs.cpp (applyTask and cb_transform of ToolTab_0
// to ToolTab_4, the button lists), src/gui/grideditor.cpp (setRecursive,
// setLayer) and src/gui/guigridtype.cpp (defaultSize).
#include "shapelogic/app/shape_tools.h"

#include "shapelogic/base/assert.h"
#include "shapelogic/base/int_cast.h"
#include "shapelogic/grid/voxel.h"

#include <array>
#include <optional>

namespace bt::app {

namespace {

struct Shift {
  int dx;
  int dy;
  int dz;
};

// The translation of a nudge on a grid, or none when the task is no nudge
// there.
std::optional<Shift> nudge(GridKind grid, ShapeTask task) {
  const auto t = static_cast<int>(task);
  switch (grid) {
    case GridKind::Bricks:
    case GridKind::Sliding:
    case GridKind::Stacking:
    case GridKind::Rhombic:
    case GridKind::TetraOcta: {
      // one cell, or one block of 5 or 6 cells of the cut grids
      const int u = grid == GridKind::Rhombic ? 5 : grid == GridKind::TetraOcta ? 6 : 1;
      switch (t) {
        case 0: return Shift{u, 0, 0};
        case 1: return Shift{-u, 0, 0};
        case 2: return Shift{0, u, 0};
        case 3: return Shift{0, -u, 0};
        case 4: return Shift{0, 0, u};
        case 5: return Shift{0, 0, -u};
        default: return std::nullopt;
      }
    }
    case GridKind::TriangularPrism:
      switch (t) {
        case 0: return Shift{1, 1, 0};
        case 1: return Shift{-1, 1, 0};
        case 2: return Shift{2, 0, 0};
        case 3: return Shift{-2, 0, 0};
        case 4: return Shift{0, 0, 1};
        case 5: return Shift{0, 0, -1};
        case 28: return Shift{-1, -1, 0};
        case 27: return Shift{1, -1, 0};
        default: return std::nullopt;
      }
    case GridKind::Spheres:
      switch (t) {
        case 0: return Shift{1, 0, 1};
        case 1: return Shift{0, 1, 1};
        case 2: return Shift{-1, 0, 1};
        case 3: return Shift{0, -1, 1};
        case 4: return Shift{1, 1, 0};
        case 5: return Shift{-1, 1, 0};
        case 27: return Shift{-1, -1, 0};
        case 28: return Shift{1, -1, 0};
        case 29: return Shift{1, 0, -1};
        case 30: return Shift{0, 1, -1};
        case 31: return Shift{-1, 0, -1};
        case 32: return Shift{0, -1, -1};
        default: return std::nullopt;
      }
  }
  return std::nullopt;
}

// The transformation of a rotation or flip on a grid, or none.
std::optional<unsigned int> turn(GridKind grid, ShapeTask task) {
  const auto t = static_cast<int>(task);
  switch (grid) {
    case GridKind::Bricks:
    case GridKind::Sliding:
    case GridKind::Stacking:
    case GridKind::Rhombic:
    case GridKind::TetraOcta:
      switch (t) {
        case 7: return 3;
        case 6: return 1;
        case 9: return 12;
        case 8: return 4;
        case 11: return 20;
        case 10: return 16;
        case 12: return 24;
        case 13: return 34;
        case 14: return 32;
        default: return std::nullopt;
      }
    case GridKind::TriangularPrism:
      switch (t) {
        case 6: return 9;
        case 9: return 6;
        case 11: return 1;
        case 10: return 5;
        case 12: return 12;
        case 13: return 15;
        case 14: return 18;
        default: return std::nullopt;
      }
    case GridKind::Spheres:
      switch (t) {
        case 7: return 9;
        case 6: return 14;
        case 9: return 5;
        case 8: return 16;
        case 11: return 2;
        case 10: return 6;
        case 12: return 120;
        case 13: return 124;
        case 14: return 141;
        default: return std::nullopt;
      }
  }
  return std::nullopt;
}

bool isCubeLike(GridKind g) {
  return g == GridKind::Bricks || g == GridKind::Sliding || g == GridKind::Stacking;
}

// "To origin" (task 24) of each grid.
void toOrigin(Voxel& space, GridKind grid) {
  const int bx1 = asInt(space.boundX1());
  const int by1 = asInt(space.boundY1());
  const int bz1 = asInt(space.boundZ1());
  switch (grid) {
    case GridKind::Bricks:
    case GridKind::Sliding:
    case GridKind::Stacking:
      space.translate(-bx1, -by1, -bz1, 0);
      return;
    case GridKind::TriangularPrism: {
      int dx = -bx1;
      int dy = -by1;
      // an even move only, otherwise the triangles turn over
      if (((dx + dy) & 1) != 0) {
        if (dx < 0) {
          dx++;
        } else {
          dy++;
        }
      }
      space.translate(dx, dy, -bz1, 0);
      return;
    }
    case GridKind::Spheres: {
      int dx = -bx1;
      int dy = -by1;
      int dz = -bz1;
      // an even move only, otherwise the spheres leave the grid
      if (((dx + dy + dz) & 1) != 0) {
        if (dx < 0) {
          dx++;
        } else if (dy < 0) {
          dy++;
        } else {
          dz++;
        }
      }
      space.translate(dx, dy, dz, 0);
      return;
    }
    case GridKind::Rhombic:
      space.translate(-(bx1 / 5) * 5, -(by1 / 5) * 5, -(bz1 / 5) * 5, 0);
      return;
    case GridKind::TetraOcta:
      space.translate(-(bx1 / 6) * 6, -(by1 / 6) * 6, -(bz1 / 6) * 6, 0);
      return;
  }
}

// "Centre" (task 25) of each grid.
void centre(Voxel& space, GridKind grid) {
  // an empty space stays
  if (space.boundX2() < space.boundX1()) {
    return;
  }
  const int sx = asInt(space.getX());
  const int sy = asInt(space.getY());
  const int sz = asInt(space.getZ());
  const int bx1 = asInt(space.boundX1());
  const int by1 = asInt(space.boundY1());
  const int bz1 = asInt(space.boundZ1());
  const int bx2 = asInt(space.boundX2());
  const int by2 = asInt(space.boundY2());
  const int bz2 = asInt(space.boundZ2());

  if (grid == GridKind::Rhombic || grid == GridKind::TetraOcta) {
    // to the nearest block boundary
    const int u = grid == GridKind::Rhombic ? 5 : 6;
    const int half = grid == GridKind::Rhombic ? 2 : 3;
    int fx = (sx - (bx2 - bx1 + 1)) / 2 - bx1;
    int fy = (sy - (by2 - by1 + 1)) / 2 - by1;
    int fz = (sz - (bz2 - bz1 + 1)) / 2 - bz1;
    if (fx % u <= half) {
      fx -= fx % u;
    } else if (bx2 + (u - fx % u) < sx) {
      fx += (u - fx % u);
    }
    if (fy % u <= half) {
      fy -= fy % u;
    } else if (by2 + (u - fy % u) < sy) {
      fy += (u - fy % u);
    }
    if (fz % u <= half) {
      fz -= fz % u;
    } else if (bz2 + (u - fz % u) < sz) {
      fz += (u - fz % u);
    }
    space.translate(fx, fy, fz, 0);
    return;
  }

  int fx = sx - (bx2 - bx1 + 1);
  int fy = sy - (by2 - by1 + 1);
  int fz = sz - (bz2 - bz1 + 1);

  if ((fx & 1) != 0 || (fy & 1) != 0 || (fz & 1) != 0) {
    space.resize(space.getX() + asUnsigned(fx & 1), space.getY() + asUnsigned(fy & 1),
                 space.getZ() + asUnsigned(fz & 1), 0);
    fx += fx & 1;
    fy += fy & 1;
    fz += fz & 1;
  }
  // the bounding box after the resize
  const int nbx1 = asInt(space.boundX1());
  const int nby1 = asInt(space.boundY1());
  const int nbz1 = asInt(space.boundZ1());
  // prisms and spheres move by an even amount only
  const bool prisms = grid == GridKind::TriangularPrism;
  const bool spheres = grid == GridKind::Spheres;
  if ((prisms && (((fx / 2 - nbx1) + (fy / 2 - nby1)) & 1) != 0) ||
      (spheres && (((fx / 2 - nbx1) + (fy / 2 - nby1) + (fz / 2 - nbz1)) & 1) != 0)) {
    space.resize(space.getX() + 2, space.getY(), space.getZ(), 0);
    fx += 2;
  }
  space.translate(fx / 2 - asInt(space.boundX1()), fy / 2 - asInt(space.boundY1()),
                  fz / 2 - asInt(space.boundZ1()), 0);
}

constexpr ShapeTaskInfo kFlips[] = {
    {ShapeTask::Flip12, "Flip", "Flip along Y-Z Plane", false},
    {ShapeTask::Flip13, "Flip", "Flip along X-Z Plane", false},
    {ShapeTask::Flip14, "Flip", "Flip along X-Y Plane", false},
};

constexpr ShapeTaskInfo kNudgesCube[] = {
    {ShapeTask::Nudge1, "Nudge", "Shift down along X", false},
    {ShapeTask::Nudge0, "Nudge", "Shift up along X", false},
    {ShapeTask::Nudge3, "Nudge", "Shift down along Y", false},
    {ShapeTask::Nudge2, "Nudge", "Shift up along Y", false},
    {ShapeTask::Nudge5, "Nudge", "Shift down along Z", false},
    {ShapeTask::Nudge4, "Nudge", "Shift up along Z", false},
};

constexpr ShapeTaskInfo kNudgesPrisms[] = {
    {ShapeTask::Nudge1, "Nudge", "Shift up left along XY plane", false},
    {ShapeTask::Nudge0, "Nudge", "Shift up right along XY plane", false},
    {ShapeTask::Nudge3, "Nudge", "Shift left along X", false},
    {ShapeTask::Nudge2, "Nudge", "Shift right along X", false},
    {ShapeTask::Nudge28, "Nudge", "Shift down left XY plane", false},
    {ShapeTask::Nudge27, "Nudge", "Shift down right XY plane", false},
    {ShapeTask::Nudge5, "Nudge", "Shift down along Z", false},
    {ShapeTask::Nudge4, "Nudge", "Shift up along Z", false},
};

constexpr ShapeTaskInfo kNudgesSpheres[] = {
    {ShapeTask::Nudge0, "Nudge", "Shift up along Z and right along X", false},
    {ShapeTask::Nudge1, "Nudge", "Shift up along Z and up along Y", false},
    {ShapeTask::Nudge2, "Nudge", "Shift up along Z and left along X", false},
    {ShapeTask::Nudge3, "Nudge", "Shift up along Z and down along Y", false},
    {ShapeTask::Nudge4, "Nudge", "Shift up right along XY plane", false},
    {ShapeTask::Nudge5, "Nudge", "Shift up left along XY plane", false},
    {ShapeTask::Nudge27, "Nudge", "Shift down left along XY plane", false},
    {ShapeTask::Nudge28, "Nudge", "Shift down right along XY plane", false},
    {ShapeTask::Nudge29, "Nudge", "Shift down along Z and right along X", false},
    {ShapeTask::Nudge30, "Nudge", "Shift down along Z and up along Y", false},
    {ShapeTask::Nudge31, "Nudge", "Shift down along Z and left along X", false},
    {ShapeTask::Nudge32, "Nudge", "Shift down along Z and down along Y", false},
};

constexpr ShapeTaskInfo kRotationsPrisms[] = {
    {ShapeTask::Rotate6, "Rotate", "Rotate 180° along X-Axis", false},
    {ShapeTask::Rotate9, "Rotate", "Rotate 180° along Y-Axis", false},
    {ShapeTask::Rotate10, "Rotate", "Rotate 60° clockwise along Z-Axis", false},
    {ShapeTask::Rotate11, "Rotate", "Rotate 60° anticlockwise along Z-Axis", false},
};

constexpr ShapeTaskInfo kRotations[] = {
    {ShapeTask::Rotate6, "Rotate", "Rotate 90° clockwise along X-Axis", false},
    {ShapeTask::Rotate7, "Rotate", "Rotate 90° anticlockwise along X-Axis", false},
    {ShapeTask::Rotate9, "Rotate", "Rotate 90° clockwise along Y-Axis", false},
    {ShapeTask::Rotate8, "Rotate", "Rotate 90° anticlockwise along Y-Axis", false},
    {ShapeTask::Rotate10, "Rotate", "Rotate 90° clockwise along Z-Axis", false},
    {ShapeTask::Rotate11, "Rotate", "Rotate 90° anticlockwise along Z-Axis", false},
};

constexpr ShapeTaskInfo kGrid[] = {
    {ShapeTask::Prune, "Grid", "Minimize size of grid", true},
    {ShapeTask::Centre, "Grid", "Centre shape inside the grid", true},
    {ShapeTask::ToOrigin, "Grid", "Move shape to origin of grid", true},
};

constexpr ShapeTaskInfo kMinimize = {ShapeTask::Minimize, "Shape", "Try to minimize size of shape",
                                     true};
constexpr ShapeTaskInfo kScales[] = {
    {ShapeTask::Double, "Shape", "Double size of shape", true},
    {ShapeTask::Triple, "Shape", "Triple size of shape", true},
};

constexpr ShapeTaskInfo kConstrain[] = {
    {ShapeTask::InsideFixed, "Constrain", "Make inside fixed", false},
    {ShapeTask::InsideVariable, "Constrain", "Make inside variable", false},
    {ShapeTask::InsideDecolour, "Constrain", "Remove Colours from inside voxels", false},
    {ShapeTask::OutsideFixed, "Constrain", "Make outside fixed", false},
    {ShapeTask::OutsideVariable, "Constrain", "Make outside variable", false},
    {ShapeTask::OutsideDecolour, "Constrain", "Remove Colours from outside voxels", false},
};

constexpr ShapeTaskInfo kFillHoles = {
    ShapeTask::FillHoles, "Tools",
    "Fill every empty voxel that is closed in by the shape, so that it cannot be reached from "
    "outside",
    false};
constexpr ShapeTaskInfo kGridScale = {
    ShapeTask::GridScale, "Tools",
    "Scale the shape up, keeping only its surface and the edges of each original voxel, so that "
    "the voxels show as a grid",
    false};

template <class C>
void append(std::vector<ShapeTaskInfo>& v, const C& c) {
  v.insert(v.end(), std::begin(c), std::end(c));
}

// The number of cells whose byte (state and colour) differs; the spaces
// have the same size.
unsigned int cellsDiffering(const Voxel& a, const Voxel& b) {
  unsigned int n = 0;
  for (unsigned int i = 0; i < a.getXYZ(); i++) {
    if (a.getState(i) != b.getState(i) || a.getColor(i) != b.getColor(i)) {
      n++;
    }
  }
  return n;
}

}  // namespace

std::vector<ShapeTaskInfo> taskSet(GridKind grid) {
  std::vector<ShapeTaskInfo> v;
  append(v, kFlips);
  switch (grid) {
    case GridKind::TriangularPrism:
      append(v, kNudgesPrisms);
      append(v, kRotationsPrisms);
      break;
    case GridKind::Spheres:
      append(v, kNudgesSpheres);
      append(v, kRotations);
      break;
    default:
      append(v, kNudgesCube);
      append(v, kRotations);
      break;
  }
  append(v, kGrid);
  // Minimize: the cube grid only (the others said "not implemented");
  // spheres have no scale buttons
  if (isCubeLike(grid)) {
    v.push_back(kMinimize);
  }
  if (grid != GridKind::Spheres) {
    append(v, kScales);
  }
  append(v, kConstrain);
  v.push_back(kFillHoles);
  // grid scale: not on the sphere and tetra-octa grids (the button did
  // nothing there)
  if (grid != GridKind::Spheres && grid != GridKind::TetraOcta) {
    v.push_back(kGridScale);
  }
  return v;
}

bool taskOffered(GridKind grid, ShapeTask task) {
  for (const ShapeTaskInfo& t : taskSet(grid)) {
    if (t.task == task) {
      return true;
    }
  }
  return false;
}

Result<TaskOutcome> applyTask(Voxel& v, ShapeTask task) {
  const GridKind grid = v.getGridType().getType();
  if (!taskOffered(grid, task)) {
    return fail(Errc::Unsupported, "this grid has no such shape operation");
  }
  BT_ASSERT_MSG(task != ShapeTask::Minimize, "minimize goes through minimizeShapes");

  const std::unique_ptr<Voxel> before = v.clone();

  if (const std::optional<Shift> s = nudge(grid, task)) {
    // filled cells may not leave the grid
    if (v.boundX2() >= v.boundX1()) {
      const int nx1 = asInt(v.boundX1()) + s->dx;
      const int ny1 = asInt(v.boundY1()) + s->dy;
      const int nz1 = asInt(v.boundZ1()) + s->dz;
      const int nx2 = asInt(v.boundX2()) + s->dx;
      const int ny2 = asInt(v.boundY2()) + s->dy;
      const int nz2 = asInt(v.boundZ2()) + s->dz;
      if (nx1 < 0 || ny1 < 0 || nz1 < 0 || nx2 >= asInt(v.getX()) || ny2 >= asInt(v.getY()) ||
          nz2 >= asInt(v.getZ())) {
        return fail(Errc::InvalidArgument, "the shape would leave the grid");
      }
    }
    v.translate(s->dx, s->dy, s->dz, 0);
  } else if (const std::optional<unsigned int> t = turn(grid, task)) {
    (void)v.transform(*t);
  } else {
    switch (task) {
      case ShapeTask::Prune:
        v.minimizePiece();
        break;
      case ShapeTask::InsideFixed:
        v.actionOnSpace(Voxel::Action::Fixed, true);
        break;
      case ShapeTask::OutsideFixed:
        v.actionOnSpace(Voxel::Action::Fixed, false);
        break;
      case ShapeTask::InsideVariable:
        v.actionOnSpace(Voxel::Action::Variable, true);
        break;
      case ShapeTask::OutsideVariable:
        v.actionOnSpace(Voxel::Action::Variable, false);
        break;
      case ShapeTask::InsideDecolour:
        v.actionOnSpace(Voxel::Action::Decolor, true);
        break;
      case ShapeTask::OutsideDecolour:
        v.actionOnSpace(Voxel::Action::Decolor, false);
        break;
      case ShapeTask::Double:
        v.scale(2);
        break;
      case ShapeTask::Triple:
        v.scale(3);
        break;
      case ShapeTask::ToOrigin:
        toOrigin(v, grid);
        break;
      case ShapeTask::Centre:
        centre(v, grid);
        break;
      case ShapeTask::FillHoles:
        v.fillHoles(0);
        break;
      case ShapeTask::GridScale:
        // the rhombic grid's blocks are 5 wide: 7 keeps their edges
        v.scale(grid == GridKind::Rhombic ? 7 : 5, true);
        break;
      default:
        BT_ASSERT_MSG(false, "a task in the set without an action");
    }
  }
  v.initHotspot();

  TaskOutcome out;
  const bool sameSize =
      v.getX() == before->getX() && v.getY() == before->getY() && v.getZ() == before->getZ();
  out.cellsChanged = sameSize ? cellsDiffering(*before, v) : 0;
  out.changed = !(v == *before) || v.getHx() != before->getHx() || v.getHy() != before->getHy() ||
                v.getHz() != before->getHz();
  return out;
}

void minimizeShapes(const std::vector<Voxel*>& shapes) {
  static constexpr std::array<unsigned char, 8> kPrimes = {2, 3, 5, 7, 11, 13, 17, 19};
  std::size_t prime = 0;
  while (prime < kPrimes.size()) {
    bool canScale = true;
    for (Voxel* s : shapes) {
      if (!s->scaleDown(kPrimes[prime], false)) {
        canScale = false;
        break;
      }
    }
    if (canScale) {
      for (Voxel* s : shapes) {
        (void)s->scaleDown(kPrimes[prime], true);
      }
    } else {
      prime++;
    }
  }
  for (Voxel* s : shapes) {
    s->initHotspot();
  }
}

unsigned int applyCellTool(Voxel& v, CellTool tool, unsigned int colour, std::uint8_t symmetry,
                           int x, int y, int z) {
  unsigned int changed = 0;
  if (symmetry == 0) {
    // a cell outside the space, or not valid in the grid, stays
    if (x < 0 || y < 0 || z < 0 || x >= asInt(v.getX()) || y >= asInt(v.getY()) ||
        z >= asInt(v.getZ()) || !v.validCoordinate(x, y, z)) {
      return 0;
    }
    const auto ux = asUnsigned(x);
    const auto uy = asUnsigned(y);
    const auto uz = asUnsigned(z);
    int state = Voxel::kEmpty;
    if (tool == CellTool::Fixed) {
      state = Voxel::kFilled;
    } else if (tool == CellTool::Variable) {
      state = Voxel::kVariable;
    }
    bool cellChanged = false;
    if (tool != CellTool::Paint && v.getState(ux, uy, uz) != state) {
      cellChanged = true;
      v.setState(ux, uy, uz, state);
      // an erased cell keeps no colour
      if (state == Voxel::kEmpty) {
        v.setColor(ux, uy, uz, 0);
      }
    }
    if (v.getState(ux, uy, uz) != Voxel::kEmpty && v.getColor(ux, uy, uz) != colour) {
      cellChanged = true;
      v.setColor(ux, uy, uz, colour);
    }
    return cellChanged ? 1 : 0;
  }
  // the mirror tools: both cells, the bit cleared
  if ((symmetry & kMirrorX) != 0) {
    const auto rest = static_cast<std::uint8_t>(symmetry & ~kMirrorX);
    changed += applyCellTool(v, tool, colour, rest, x, y, z);
    changed += applyCellTool(v, tool, colour, rest, asInt(v.getX()) - x - 1, y, z);
  } else if ((symmetry & kMirrorY) != 0) {
    const auto rest = static_cast<std::uint8_t>(symmetry & ~kMirrorY);
    changed += applyCellTool(v, tool, colour, rest, x, y, z);
    changed += applyCellTool(v, tool, colour, rest, x, asInt(v.getY()) - y - 1, z);
  } else if ((symmetry & kMirrorZ) != 0) {
    const auto rest = static_cast<std::uint8_t>(symmetry & ~kMirrorZ);
    changed += applyCellTool(v, tool, colour, rest, x, y, z);
    changed += applyCellTool(v, tool, colour, rest, x, y, asInt(v.getZ()) - z - 1);
  } else {
    // the spans last: all three at once, each along its own row only
    if ((symmetry & kSpanX) != 0) {
      for (int xp = 0; xp < asInt(v.getX()); xp++) {
        changed += applyCellTool(v, tool, colour, 0, xp, y, z);
      }
    }
    if ((symmetry & kSpanY) != 0) {
      for (int yp = 0; yp < asInt(v.getY()); yp++) {
        changed += applyCellTool(v, tool, colour, 0, x, yp, z);
      }
    }
    if ((symmetry & kSpanZ) != 0) {
      for (int zp = 0; zp < asInt(v.getZ()); zp++) {
        changed += applyCellTool(v, tool, colour, 0, x, y, zp);
      }
    }
  }
  return changed;
}

unsigned int editCells(Voxel& v, CellTool tool, unsigned int colour, std::uint8_t symmetry,
                       const std::vector<CellEdit>& cells) {
  unsigned int changed = 0;
  for (const CellEdit& c : cells) {
    changed += applyCellTool(v, tool, colour, symmetry, c.x, c.y, c.z);
  }
  if (changed > 0) {
    v.initHotspot();
  }
  return changed;
}

unsigned int defaultShapeSize(GridKind grid) {
  switch (grid) {
    case GridKind::Rhombic:
      return 5;
    case GridKind::Spheres:
      return 3;
    case GridKind::Sliding:
      return 6;
    default:
      return 4;
  }
}

unsigned int sizeUnit(GridKind grid) {
  switch (grid) {
    case GridKind::Rhombic:
      return 5;
    case GridKind::TetraOcta:
      return 3;
    default:
      return 1;
  }
}

}  // namespace bt::app
