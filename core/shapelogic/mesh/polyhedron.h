// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// @file
/// A polygon mesh in a half-edge structure: what the meshes of the shapes
/// are built in before they are handed out as plain triangle lists.
///
/// Ported from src/halfedge/ (Polyhedron, Face, HalfEdge, Vertex: a
/// simplified HalfEdgeLib by Pablo Diaz-Gutierrez, with ShapeLogic'
/// finalize). The original linked its objects by owning raw pointers; here
/// they live in arrays and refer to each other by number. Faces and
/// half-edges keep the original's order rules: they are listed in the order
/// they were added, and erasing one moves the last into its place, so a
/// mesh comes out in the same order as the original's.

#include "shapelogic/mesh/vec3.h"

#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace bt::mesh {

/// Face flags (FF_ of the original voxel.h): how the 3D view draws a face.
enum FaceFlag : std::uint32_t {
  kFaceColorLight = 0x01,    ///< the lighter colour of the checker pattern
  kFaceVariableMark = 0x02,  ///< a variable cell: draw the variable marker on it
  kFaceVariable = 0x04,      ///< the face itself is the variable marker
  kFaceWireframe = 0x08,     ///< stays when the shape is drawn as a wire frame
  kFaceBevel = 0x10,         ///< added for the bevel of a cell edge
  kFaceOffset = 0x20,        ///< added for the gap between neighbouring cells
  kFaceProcessed = 0x40,     ///< seen by the hole filling
  kFaceInside = 0x80,        ///< the inner side of a hollow shape
};

/// "None": a missing face, half-edge or vertex.
inline constexpr int kNone = -1;

/// One side of an edge, running around the face it belongs to (counter
/// clockwise seen from outside).
struct HalfEdge {
  int dst = kNone;   ///< the vertex it ends at
  int face = kNone;  ///< the face (or hole) it runs around
  int twin = kNone;  ///< the other side of the edge
  int next = kNone;  ///< the next half-edge of the face
  int index = kNone; ///< its place in the list of half-edges
};

/// A corner point.
struct Vertex {
  Vec3 pos;
  int edge = kNone;  ///< one half-edge that starts here
  int index = kNone;
};

/// A face, or a hole that closes the surface.
struct Face {
  int edge = kNone;  ///< one half-edge of its boundary
  bool hole = false;
  int index = kNone;  ///< its place in the list of faces
  std::uint32_t flags = 0;  ///< FaceFlag bits
  /// The cell the face belongs to (the voxel index) and which side of it
  /// (the neighbour number; -1 for bevel, offset and filling faces): what a
  /// click on the face picks.
  std::uint32_t voxel = 0;
  std::int32_t voxelFace = 0;
  /// The constraint colour of the cell (0: none).
  std::uint8_t colour = 0;
};

/// The mesh. Faces and half-edges are named by a number that stays while
/// they exist (their id); `index` is their place in the list, which an
/// erase changes. Vertices are never erased: their id is their index.
class Polyhedron {
 public:
  /// Adds a vertex without connections; returns its index.
  int addVertex(const Vec3& pos);
  /// Adds a face through the vertices `corners` (in order), with one new
  /// half-edge per side; twins are linked by finalize(). Returns its id.
  int addFace(const std::vector<int>& corners);

  /// Removes a face (its half-edges must be gone already).
  void eraseFace(int face);
  /// Removes a half-edge (no other one may refer to it any more).
  void eraseHalfEdge(int he);

  /// Links the twins of all half-edges (also where an even number of faces
  /// meet at one edge, sorted by their angle around it) and closes what is
  /// left open with hole faces.
  void finalize();

  [[nodiscard]] int numVertices() const { return static_cast<int>(vertices_.size()); }
  [[nodiscard]] int numFaces() const { return static_cast<int>(faces_.size()); }
  [[nodiscard]] int numHalfEdges() const { return static_cast<int>(halfEdges_.size()); }
  /// The number of faces that are holes.
  [[nodiscard]] int numHoles() const;

  /// The face, half-edge or vertex at place `i` of its list: its id.
  [[nodiscard]] int faceAt(int i) const { return faces_[static_cast<std::size_t>(i)]; }
  [[nodiscard]] int halfEdgeAt(int i) const {
    return halfEdges_[static_cast<std::size_t>(i)];
  }

  [[nodiscard]] Face& face(int id) { return faceStore_[static_cast<std::size_t>(id)]; }
  [[nodiscard]] const Face& face(int id) const { return faceStore_[static_cast<std::size_t>(id)]; }
  [[nodiscard]] HalfEdge& he(int id) { return heStore_[static_cast<std::size_t>(id)]; }
  [[nodiscard]] const HalfEdge& he(int id) const { return heStore_[static_cast<std::size_t>(id)]; }
  [[nodiscard]] Vertex& vertex(int i) { return vertices_[static_cast<std::size_t>(i)]; }
  [[nodiscard]] const Vertex& vertex(int i) const { return vertices_[static_cast<std::size_t>(i)]; }

  /// The half-edge before `h` in its face.
  [[nodiscard]] int prev(int h) const;
  /// The vertex a half-edge starts at (through its twin).
  [[nodiscard]] int src(int h) const { return he(he(h).twin).dst; }
  /// The position of the vertex a half-edge ends at.
  [[nodiscard]] const Vec3& dstPos(int h) const { return vertex(he(h).dst).pos; }

  /// The normal of a face, from its first three corners.
  [[nodiscard]] Vec3 faceNormal(int f) const;
  /// The number of corners of a face.
  [[nodiscard]] int faceSize(int f) const;

  /// Checks the structure; true when sound (BT_ASSERTs on the first fault).
  [[nodiscard]] bool check(bool holesFilled) const;

 private:
  void closeSurface();

  std::vector<Vertex> vertices_;
  std::vector<Face> faceStore_;
  std::vector<HalfEdge> heStore_;
  /// The ids of the faces and half-edges that exist, in list order.
  std::vector<int> faces_;
  std::vector<int> halfEdges_;
};

/// Finds or adds vertices by position: a point already in the mesh gets
/// the same vertex (vertexList_c of the original).
class VertexList {
 public:
  explicit VertexList(Polyhedron& poly);
  /// The vertex at (x, y, z), added when new.
  int get(float x, float y, float z);

 private:
  Polyhedron& poly_;
  std::map<Vec3, int> known_;
};

/// Removes faces with all their half-edges (eraseFaces of the original,
/// which went through them in the order of their addresses; here in the
/// order they were made).
void eraseFaces(Polyhedron& poly, const std::set<int>& faces);

}  // namespace bt::mesh
