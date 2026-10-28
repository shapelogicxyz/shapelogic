// SPDX-License-Identifier: GPL-3.0-or-later
// The `shapelogic` Python module: the original module's classes, functions
// and names over the new core, so its scripts keep working; the grid of a
// new puzzle, the solve's threads and the sliding and stacking options,
// the imports, the Puzzlecad export and grid conversion are new.
// Ported from src/python/module.cpp.
#include "py_problem.h"
#include "py_puzzle.h"
#include "py_shape.h"
#include "py_solution.h"
#include "solution_iterator.h"

#include "shapelogic/base/version.h"
#include "shapelogic/grid/voxel.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <optional>
#include <stdexcept>
#include <string>

namespace py = pybind11;
using namespace bt::python;

namespace {

bt::stacking::StackingSolver stackingSolverByName(const std::string& name) {
  if (name == "stacking") {
    return bt::stacking::StackingSolver::Shortest;
  }
  if (name == "panex") {
    return bt::stacking::StackingSolver::Panex;
  }
  throw std::invalid_argument("stacking_solver must be 'stacking' or 'panex'");
}

// The one voxel state enum the module had; its values are the core's.
enum class VoxelState : int {
  Empty = bt::Voxel::kEmpty,
  Filled = bt::Voxel::kFilled,
  Variable = bt::Voxel::kVariable,
};

}  // namespace

PYBIND11_MODULE(shapelogic, m) {
  m.doc() = "Python bindings for the ShapeLogic 3D interlocking puzzle library";
  m.attr("__version__") = std::string(bt::kVersion);

  // Voxel state enum
  py::enum_<VoxelState>(m, "Voxel", "Voxel state values")
      .value("EMPTY", VoxelState::Empty)
      .value("FILLED", VoxelState::Filled)
      .value("VARIABLE", VoxelState::Variable)
      .export_values();

  // Placement
  py::class_<PyPlacement>(m, "Placement", "A piece placement within an assembly")
      .def_readonly("piece_id", &PyPlacement::pieceId, "Index of the piece")
      .def_readonly("x", &PyPlacement::x, "X coordinate")
      .def_readonly("y", &PyPlacement::y, "Y coordinate")
      .def_readonly("z", &PyPlacement::z, "Z coordinate")
      .def_readonly("transformation", &PyPlacement::transformation,
                    "Orientation/transformation index")
      .def_readonly("is_placed", &PyPlacement::isPlaced,
                    "Whether this piece is placed in the assembly")
      .def("__repr__", [](const PyPlacement& p) {
        return "<Placement piece=" + std::to_string(p.pieceId) + " pos=(" + std::to_string(p.x) +
               "," + std::to_string(p.y) + "," + std::to_string(p.z) + ")" +
               " trans=" + std::to_string(p.transformation) +
               (p.isPlaced ? " placed>" : " unplaced>");
      });

  // Solution
  py::class_<PySolution>(m, "Solution", "A puzzle solution found by the solver")
      .def_readonly("assembly_number", &PySolution::assemblyNumber,
                    "Sequence number of the assembly")
      .def_readonly("solution_number", &PySolution::solutionNumber,
                    "Sequence number among valid solutions")
      .def_readonly("has_disassembly", &PySolution::hasDisassembly,
                    "Whether disassembly analysis was performed")
      .def_readonly("moves_text", &PySolution::movesText,
                    "Disassembly sequence string (e.g. '14.2.1')")
      .def_readonly("total_moves", &PySolution::totalMoves,
                    "Total number of moves to fully disassemble")
      .def_readonly("level", &PySolution::level, "Number of moves to remove the first piece")
      .def_readonly("placements", &PySolution::placements,
                    "List of piece placements in the assembly")
      .def("__repr__", [](const PySolution& s) {
        std::string res = "<Solution #" + std::to_string(s.solutionNumber);
        if (s.hasDisassembly) {
          res += " level=" + std::to_string(s.level) + " moves='" + s.movesText + "'";
        }
        res += " pieces=" + std::to_string(s.placements.size()) + ">";
        return res;
      });

  // SolutionIterator
  py::class_<SolutionIterator>(m, "SolutionIterator",
                               "Streaming iterator yielding solutions as they are found")
      .def("__iter__", [](SolutionIterator& it) -> SolutionIterator& { return it; },
           py::return_value_policy::reference_internal)
      .def("__next__", &SolutionIterator::next)
      .def("stop", &SolutionIterator::stop, "Stop the background solving process immediately")
      .def_property_readonly("is_finished", &SolutionIterator::isFinished,
                             "Whether the solver has finished searching")
      .def_property_readonly("iterations", &SolutionIterator::getIterations,
                             "Number of internal solver iterations")
      .def("__repr__", [](const SolutionIterator& it) {
        return "<SolutionIterator finished=" + std::string(it.isFinished() ? "True" : "False") +
               " iterations=" + std::to_string(it.getIterations()) + ">";
      });

  // Shape
  py::class_<PyShape>(m, "Shape", "A 3D voxel grid representing a puzzle shape or piece")
      .def_property("name", &PyShape::getName, &PyShape::setName, "Name or label of the shape")
      .def_property_readonly("index", &PyShape::getIndex, "Index of the shape in the puzzle")
      .def_property_readonly("size_x", &PyShape::getSizeX, "Width along X axis")
      .def_property_readonly("size_y", &PyShape::getSizeY, "Height along Y axis")
      .def_property_readonly("size_z", &PyShape::getSizeZ, "Depth along Z axis")
      .def_property_readonly("dimensions", &PyShape::getDimensions, "Dimensions as (x, y, z) tuple")
      .def("get", &PyShape::get, py::arg("x"), py::arg("y"), py::arg("z"),
           "Get raw voxel value at (x, y, z)")
      .def("set", &PyShape::set, py::arg("x"), py::arg("y"), py::arg("z"), py::arg("val"),
           "Set raw voxel value at (x, y, z)")
      .def("get_state", &PyShape::getState, py::arg("x"), py::arg("y"), py::arg("z"),
           "Get voxel state (EMPTY, FILLED, VARIABLE)")
      .def("set_state", &PyShape::setState, py::arg("x"), py::arg("y"), py::arg("z"),
           py::arg("state"), "Set voxel state (EMPTY, FILLED, VARIABLE)")
      .def("get_color", &PyShape::getColor, py::arg("x"), py::arg("y"), py::arg("z"),
           "Get voxel constraint color index")
      .def("set_color", &PyShape::setColor, py::arg("x"), py::arg("y"), py::arg("z"),
           py::arg("color"), "Set voxel constraint color index")
      .def("is_filled", &PyShape::isFilled, py::arg("x"), py::arg("y"), py::arg("z"),
           "Check if voxel is filled")
      .def("is_empty", &PyShape::isEmpty, py::arg("x"), py::arg("y"), py::arg("z"),
           "Check if voxel is empty")
      .def("is_variable", &PyShape::isVariable, py::arg("x"), py::arg("y"), py::arg("z"),
           "Check if voxel is variable")
      .def("fill", &PyShape::fill, py::arg("coords"),
           "Fill voxels at all given (x, y, z) coordinate tuples")
      .def("count_filled", &PyShape::countFilled, "Number of filled voxels in this shape")
      .def("count_empty", &PyShape::countEmpty, "Number of empty voxels in this shape")
      .def("__repr__", [](const PyShape& s) {
        return "<Shape #" + std::to_string(s.getIndex()) + " '" + s.getName() + "' size=(" +
               std::to_string(s.getSizeX()) + "," + std::to_string(s.getSizeY()) + "," +
               std::to_string(s.getSizeZ()) + ")" + " filled=" + std::to_string(s.countFilled()) +
               ">";
      });

  // Problem
  py::class_<PyProblem>(m, "Problem", "A puzzle problem defining target shape and piece constraints")
      .def_property("name", &PyProblem::getName, &PyProblem::setName, "Name of the problem")
      .def_property_readonly("num_pieces", &PyProblem::getNumPieces,
                             "Number of pieces in this problem")
      .def_property_readonly("index", &PyProblem::getIndex, "Zero-based problem index in the puzzle")
      .def_property_readonly("result_shape_index", &PyProblem::getResult,
                             "Shape index of the target result shape, or -1 if unset")
      .def("set_result", &PyProblem::setResult, py::arg("shape_index"),
           "Set the target result shape by index")
      .def("set_result", &PyProblem::setResultShape, py::arg("shape"), "Set the target result shape")
      .def("set_piece_count", &PyProblem::setPieceCount, py::arg("shape_index"), py::arg("count"),
           "Set fixed piece count for a shape index")
      .def("set_piece_count", &PyProblem::setPieceCountShape, py::arg("shape"), py::arg("count"),
           "Set fixed piece count for a shape")
      .def("set_piece_range", &PyProblem::setPieceRange, py::arg("shape_index"),
           py::arg("min_count"), py::arg("max_count"),
           "Set min and max piece count for a shape index")
      .def("set_piece_range", &PyProblem::setPieceRangeShape, py::arg("shape"),
           py::arg("min_count"), py::arg("max_count"), "Set min and max piece count for a shape")
      .def("get_piece_min", &PyProblem::getPieceMin, py::arg("shape_index"),
           "Get minimum piece count for a shape index")
      .def("get_piece_max", &PyProblem::getPieceMax, py::arg("shape_index"),
           "Get maximum piece count for a shape index")
      .def(
          "solve",
          [](const PyProblem& p, bool disassemble, bool reduce, bool keepRotations,
             bool keepMirror, unsigned int threads, bool nestedSlides, bool partialNested, bool verticalMoves, bool highMemory,
             const std::string& stackingSolver, const std::string& workDir) {
            IteratorOptions o;
            o.disassemble = disassemble;
            o.reduce = reduce;
            o.keepRotations = keepRotations;
            o.keepMirror = keepMirror;
            o.threads = threads;
            o.sliding.nested = nestedSlides;
            o.sliding.partialNested = partialNested;
            o.sliding.verticalMoves = verticalMoves;
            o.sliding.highMemory = highMemory;
            o.sliding.workDir = workDir;
            o.stacking.solver = stackingSolverByName(stackingSolver);
            o.stacking.highMemory = highMemory;
            o.stacking.workDir = workDir;
            return p.solve(o);
          },
          py::arg("disassemble") = true, py::arg("reduce") = false,
          py::arg("keep_rotations") = false, py::arg("keep_mirror") = false, py::kw_only(),
          py::arg("threads") = 0, py::arg("nested_slides") = false, py::arg("partial_nested") = false,
          py::arg("vertical_moves") = false, py::arg("high_memory") = false, py::arg("stacking_solver") = "stacking",
          py::arg("work_dir") = "",
          "Start solving the problem and return an iterator yielding Solution objects.\n\n"
          "The solve works on a copy of the puzzle as it is now. threads: 0 for every "
          "core. A sliding puzzle (the Sliding Puzzle Solver, the only one): "
          "nested_slides, partial_nested, vertical_moves (up and down on a tray of "
          "several layers), high_memory. A stacking puzzle: "
          "stacking_solver 'stacking' or 'panex', high_memory. work_dir: where a "
          "sliding search or a Panex search keeps its levels (empty: in memory).")
      .def("__repr__", [](const PyProblem& p) {
        return "<Problem index=" + std::to_string(p.getIndex()) + " name='" + p.getName() +
               "' pieces=" + std::to_string(p.getNumPieces()) + ">";
      });

  // Puzzle
  py::class_<PyPuzzle, std::shared_ptr<PyPuzzle>>(
      m, "Puzzle", "A ShapeLogic puzzle definition containing shapes and problems")
      .def(py::init<const std::string&>(), py::arg("grid") = "bricks",
           "Create a new empty puzzle; grid: 'bricks' (the standard 3D cubic grid), "
           "'triangular prisms', 'spheres', 'rhombic', 'tetra-octa', 'sliding' or 'stacking'")
      .def_static("load", &PyPuzzle::load, py::arg("filename"),
                  "Load a puzzle from an .xmpuzzle file")
      .def("save", &PyPuzzle::save, py::arg("filename"), "Save the puzzle to an .xmpuzzle file")
      .def_property("comment", &PyPuzzle::getComment, &PyPuzzle::setComment,
                    "Puzzle description or comments")
      .def_property_readonly("grid", &PyPuzzle::getGrid, "Name of the puzzle's grid")
      .def_property_readonly("num_shapes", &PyPuzzle::getNumShapes, "Number of shapes in the puzzle")
      .def_property_readonly("num_problems", &PyPuzzle::getNumProblems,
                             "Number of problems in the puzzle")
      .def("add_shape", &PyPuzzle::addShape, py::arg("sx"), py::arg("sy"), py::arg("sz"),
           py::arg("name") = "",
           "Add a new empty shape of size (sx, sy, sz) to the puzzle and return it")
      .def_property_readonly("shapes", &PyPuzzle::getShapes, "List of all shapes in the puzzle")
      .def("shape", &PyPuzzle::getShape, py::arg("index"), "Get a specific shape by index")
      .def("add_problem", &PyPuzzle::addProblem, py::arg("name") = "",
           "Add a new problem to the puzzle and return it")
      .def_property_readonly("problems", &PyPuzzle::getProblems,
                             "List of all problems in the puzzle")
      .def("problem", &PyPuzzle::getProblem, py::arg("index"), "Get a specific problem by index")
      .def("export_puzzlecad", &PyPuzzle::exportPuzzlecad, py::arg("problem") = std::nullopt,
           py::arg("name") = "",
           "The Puzzlecad (OpenSCAD) text of a problem's pieces (every shape when "
           "problem is None); name: the file named in its header")
      .def_property_readonly("convert_targets", &PyPuzzle::convertTargets,
                             "Names of the grids this puzzle can be converted to")
      .def("convert_grid", &PyPuzzle::convertGrid, py::arg("grid"),
           "A new puzzle with the shapes converted to another grid ('rhombic' or "
           "'tetra-octa' from bricks) and each problem's definition copied")
      .def("__repr__", [](const PyPuzzle& p) {
        return "<Puzzle problems=" + std::to_string(p.getNumProblems()) +
               " shapes=" + std::to_string(p.getNumShapes()) + ">";
      });

  m.def("load", &PyPuzzle::load, py::arg("filename"), "Load a puzzle from an .xmpuzzle file");
  m.def("import_puzzlesolver3d", &PyPuzzle::importPuzzleSolver3D, py::arg("filename"),
        "Import a PuzzleSolver3D (.puz) file as a new puzzle");
  m.def("import_puzzlecad", &PyPuzzle::importPuzzlecad, py::arg("filename"),
        "Import a Puzzlecad OpenSCAD (.scad) file as a new puzzle");
}
