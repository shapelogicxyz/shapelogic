// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Changes to a finished mesh.
/// Ported from src/halfedge/modifiers.{h,cpp} (fillPolyhedronHoles; the
/// merging of coplanar faces belongs to the exports, later).

namespace bt::mesh {

class Polyhedron;

/// Simplifies a mesh with bevels and gaps for the wire-frame view: the
/// strips of bevel and gap faces between two cell faces become one face
/// each, and the holes left behind are capped. With `fillOutsides` the
/// strips are replaced; the caps never add material outside the solid.
void fillPolyhedronHoles(Polyhedron& poly, bool fillOutsides);

}  // namespace bt::mesh
