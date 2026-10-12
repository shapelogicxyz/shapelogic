// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// Changes to a finished mesh.
/// Ported from src/halfedge/modifiers.{h,cpp} (fillPolyhedronHoles,
/// scalePolyhedron, mergeCoplanarFaces) and src/halfedge/volume.cpp.

namespace bt::mesh {

class Polyhedron;

/// Simplifies a mesh with bevels and gaps for the wire-frame view: the
/// strips of bevel and gap faces between two cell faces become one face
/// each, and the holes left behind are capped. With `fillOutsides` the
/// strips are replaced; the caps never add material outside the solid.
void fillPolyhedronHoles(Polyhedron& poly, bool fillOutsides);

/// Scales every vertex by x, y and z along the axes.
void scalePolyhedron(Polyhedron& poly, float x, float y, float z);

/// A new mesh in which each connected group of coplanar faces is merged
/// and triangulated again with fewer, larger triangles (what the STL
/// writer writes). Every vertex on a group's outline is kept, so the
/// result stays watertight against the faces around the group; a group
/// whose triangulation fails keeps its faces. Holes are dropped. `src`
/// must be finalized (its twins linked); the result is not.
[[nodiscard]] Polyhedron mergeCoplanarFaces(const Polyhedron& src);

/// The volume enclosed by a closed mesh, in single precision as the
/// original computed it (the STL dialog's "Volume").
[[nodiscard]] float volume(const Polyhedron& poly);

}  // namespace bt::mesh
