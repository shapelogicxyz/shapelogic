// SPDX-License-Identifier: GPL-3.0-or-later
// Pictures on the shapes of a sliding puzzle (decision 0038): the commands
// that set and clear them (undone and redone byte for byte), the picture
// following the cells through the tool tabs' turns, flips and nudges, and
// the file keeping it.
#include "app_test_helpers.h"

#include "shapelogic/app/document.h"
#include "shapelogic/app/shape_tools.h"
#include "shapelogic/base/base64.h"
#include "shapelogic/grid/shape_image.h"
#include "shapelogic/grid/voxel.h"
#include "shapelogic/io/puzzle_file.h"
#include "shapelogic/mesh/draw_mesh.h"
#include "shapelogic/mesh/voxel_mesh.h"
#include "shapelogic/model/puzzle_image.h"
#include "shapelogic/model/sliding.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace bt;
using namespace bt::app;
using bt::test::refused;
using bt::test::roundTrip;

namespace {

/// The start of a PNG file of w by h pixels: as much as sniffImage() reads.
std::string pngHeader(unsigned int w, unsigned int h, char tag = 'a') {
  std::string b = "\x89PNG\r\n\x1a\n";
  b += std::string("\0\0\0\x0d", 4);
  b += "IHDR";
  for (const unsigned int v : {w, h}) {
    b += static_cast<char>((v >> 24) & 0xFF);
    b += static_cast<char>((v >> 16) & 0xFF);
    b += static_cast<char>((v >> 8) & 0xFF);
    b += static_cast<char>(v & 0xFF);
  }
  b += std::string("\x08\x02\0\0\0", 5);
  b += tag;  // so two test pictures differ
  return b;
}

/// The start of a JPEG file of w by h pixels: an APP0 segment, then the frame.
std::string jpegHeader(unsigned int w, unsigned int h) {
  std::string b = "\xFF\xD8";
  b += std::string("\xFF\xE0\x00\x04\x00\x00", 6);
  b += std::string("\xFF\xC0\x00\x11\x08", 5);
  for (const unsigned int v : {h, w}) {
    b += static_cast<char>((v >> 8) & 0xFF);
    b += static_cast<char>(v & 0xFF);
  }
  b += std::string(12, '\0');
  return b;
}

/// A new one-layer shape of w by h cells with the cells given filled.
Id piece(Document& d, unsigned int w, unsigned int h, const std::vector<CellEdit>& cells) {
  const Result<Id> s = d.createShape(std::array<unsigned int, 3>{w, h, 1});
  REQUIRE(s.has_value());
  REQUIRE(d.editCells(*s, CellTool::Fixed, 0, 0, cells).has_value());
  return *s;
}

const Voxel& shapeOf(const Document& d, Id s) { return d.puzzle().getShape(d.shapeIndex(s).value()); }

/// Where in the picture (u, v, as fractions of the part shown) the middle
/// of each filled cell is, sorted: the decision's mapping, inverted.
std::vector<std::pair<long, long>> pictureAtCells(const Voxel& v) {
  const ShapeImage& im = v.getImage();
  const std::array<int, 4> m = ShapeImage::matrix(im.turn);
  std::vector<std::pair<long, long>> out;
  for (unsigned int z = 0; z < v.getZ(); z++) {
    for (unsigned int y = 0; y < v.getY(); y++) {
      for (unsigned int x = 0; x < v.getX(); x++) {
        if (v.getState(x, y, z) == Voxel::kEmpty) {
          continue;
        }
        const double qx = (x + 0.5 - im.box[0]) / im.box[2] - 0.5;
        const double qy = (y + 0.5 - im.box[1]) / im.box[3] - 0.5;
        // M is orthogonal: its inverse is its transpose
        const double px = m[0] * qx + m[2] * qy;
        const double py = m[1] * qx + m[3] * qy;
        // in thousandths, so equal points compare equal
        out.emplace_back(std::lround((px + 0.5) * 1000), std::lround((0.5 - py) * 1000));
      }
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace

TEST_CASE("shape images: PNG and JPEG sizes are read, other bytes refused", "[app][sliding]") {
  const std::optional<ImageInfo> png = sniffImage(pngHeader(640, 480));
  REQUIRE(png.has_value());
  CHECK(png->type == "image/png");
  CHECK(png->width == 640);
  CHECK(png->height == 480);

  const std::optional<ImageInfo> jpeg = sniffImage(jpegHeader(300, 200));
  REQUIRE(jpeg.has_value());
  CHECK(jpeg->type == "image/jpeg");
  CHECK(jpeg->width == 300);
  CHECK(jpeg->height == 200);

  CHECK_FALSE(sniffImage("GIF89a........................"));
  CHECK_FALSE(sniffImage(pngHeader(0, 10)));
  CHECK_FALSE(sniffImage(pngHeader(kMaxImageSide + 1, 10)));
  CHECK_FALSE(sniffImage(jpegHeader(300, 200).substr(0, 10)));
  CHECK(imageHash("abc") != imageHash("abd"));
  CHECK(imageHash("abc").size() == 16);
}

TEST_CASE("shape images: the turn matrices make the eight symmetries", "[app][sliding]") {
  for (unsigned int t = 0; t < 8; t++) {
    CHECK(ShapeImage::turnOf(ShapeImage::matrix(t)) == t);
  }
  // a quarter turn counter-clockwise takes x to y
  CHECK(ShapeImage::matrix(1) == std::array<int, 4>{0, -1, 1, 0});
  CHECK(ShapeImage::matrix(4) == std::array<int, 4>{-1, 0, 0, 1});
}

TEST_CASE("shape images: set, replaced and cleared, each undone byte for byte",
          "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id s = piece(d, 3, 2, {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 1, 0}});

  const unsigned int id = roundTrip(d, [&](Document& doc) {
    return doc.setShapeImage(s, {pngHeader(90, 60), std::nullopt, ImageFit::Stretch, {0, 0, 90, 60}});
  }).value();
  const ShapeImage& im = shapeOf(d, s).getImage();
  CHECK(im.id == static_cast<int>(id));
  CHECK(im.fit == ImageFit::Stretch);
  CHECK(im.box == std::array<int, 4>{0, 0, 3, 2});
  CHECK(im.turn == 0);
  REQUIRE(d.puzzle().findImage(im.id) != nullptr);
  CHECK(d.puzzle().findImage(im.id)->width == 90);

  // the same picture again is the same entry; a crop of it changes the shape
  roundTrip(d, [&](Document& doc) {
    return doc.setShapeImage(s, {pngHeader(90, 60), std::nullopt, ImageFit::Crop, {0, 0, 90, 60}});
  });
  CHECK(d.puzzle().images().size() == 1);

  // a second shape shows the first one's picture by id
  const Id t = piece(d, 1, 1, {{0, 0, 0}});
  roundTrip(d, [&](Document& doc) {
    return doc.setShapeImage(t, {std::nullopt, id, ImageFit::Crop, {15, 0, 60, 60}});
  });
  CHECK(shapeOf(d, t).getImage().src == std::array<int, 4>{15, 0, 60, 60});

  roundTrip(d, [&](Document& doc) { return doc.clearShapeImage(s); });
  CHECK_FALSE(shapeOf(d, s).getImage().present());
  // clearing a shape without one changes nothing
  CHECK(d.clearShapeImage(s).has_value());
}

TEST_CASE("shape images: what the command refuses", "[app][sliding]") {
  Document d(GridKind::Sliding);
  const Id s = piece(d, 2, 1, {{0, 0, 0}, {1, 0, 0}});
  const Id empty = d.createShape(std::array<unsigned int, 3>{2, 2, 1}).value();
  const std::string png = pngHeader(20, 10);
  refused(d, Errc::InvalidArgument, [&](Document& doc) {
    return doc.setShapeImage(empty, {png, std::nullopt, ImageFit::Crop, {0, 0, 20, 10}});
  });
  refused(d, Errc::InvalidArgument, [&](Document& doc) {
    return doc.setShapeImage(s, {std::string("not a picture"), std::nullopt, ImageFit::Crop, {0, 0, 1, 1}});
  });
  refused(d, Errc::InvalidArgument, [&](Document& doc) {
    return doc.setShapeImage(s, {png, std::nullopt, ImageFit::Crop, {20, 0, 20, 10}});
  });
  refused(d, Errc::InvalidArgument, [&](Document& doc) {
    return doc.setShapeImage(s, {png, std::nullopt, ImageFit::Manual, {-40, 0, 40, 10}});
  });
  refused(d, Errc::InvalidArgument, [&](Document& doc) {
    return doc.setShapeImage(s, {png, 0U, ImageFit::Crop, {0, 0, 20, 10}});
  });
  refused(d, Errc::NotFound, [&](Document& doc) {
    return doc.setShapeImage(s, {std::nullopt, 7U, ImageFit::Crop, {0, 0, 1, 1}});
  });

  Document cubes(GridKind::Bricks);
  const Id c = cubes.createShape(std::array<unsigned int, 3>{1, 1, 1}).value();
  REQUIRE(cubes.editCells(c, CellTool::Fixed, 0, 0, {{0, 0, 0}}).has_value());
  refused(cubes, Errc::InvalidArgument, [&](Document& doc) {
    return doc.setShapeImage(c, {png, std::nullopt, ImageFit::Crop, {0, 0, 20, 10}});
  });
}

TEST_CASE("shape images: the picture follows the cells through the tool tabs",
          "[app][sliding]") {
  Document d(GridKind::Sliding);
  // an L, which no turn or mirror maps onto itself, with room to nudge
  const Id s = piece(d, 5, 5, {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}, {1, 2, 0}});
  REQUIRE(d.setShapeImage(s, {pngHeader(30, 20), std::nullopt, ImageFit::Stretch, {0, 0, 30, 20}})
              .has_value());
  const std::vector<std::pair<long, long>> start = pictureAtCells(shapeOf(d, s));
  REQUIRE(start.size() == 4);

  for (const ShapeTask task :
       {ShapeTask::Rotate10, ShapeTask::Rotate11, ShapeTask::Flip12, ShapeTask::Flip13,
        ShapeTask::Flip14, ShapeTask::Nudge0, ShapeTask::Nudge3, ShapeTask::ToOrigin,
        ShapeTask::Center, ShapeTask::Prune}) {
    INFO("task " << static_cast<int>(task));
    const Result<TaskOutcome> r = d.shapeTask(s, task);
    REQUIRE(r.has_value());
    CHECK(shapeOf(d, s).getImage().present());
    CHECK(pictureAtCells(shapeOf(d, s)) == start);
  }

  // four quarter turns are no turn
  const ShapeImage before = shapeOf(d, s).getImage();
  for (int k = 0; k < 4; k++) {
    REQUIRE(d.shapeTask(s, ShapeTask::Rotate10).has_value());
  }
  CHECK(shapeOf(d, s).getImage().turn == before.turn);
  CHECK(pictureAtCells(shapeOf(d, s)) == start);

  // a duplicate shows the same picture
  const Id copy = d.duplicateShape(s).value();
  CHECK(shapeOf(d, copy).getImage() == shapeOf(d, s).getImage());
}

TEST_CASE("shape images: the file keeps the pictures shapes show", "[app][sliding][io]") {
  Document d(GridKind::Sliding);
  const Id s = piece(d, 2, 1, {{0, 0, 0}, {1, 0, 0}});
  const Id t = piece(d, 1, 1, {{0, 0, 0}});
  const std::string png = pngHeader(40, 20, 'p');
  const std::string jpg = jpegHeader(16, 16);
  REQUIRE(d.setShapeImage(s, {png, std::nullopt, ImageFit::Stretch, {0, 0, 40, 20}}).has_value());
  REQUIRE(d.setShapeImage(t, {jpg, std::nullopt, ImageFit::Crop, {0, 0, 16, 16}}).has_value());
  REQUIRE(d.shapeTask(s, ShapeTask::Rotate10).has_value());
  // the JPEG is shown by no shape now; the file leaves it out
  REQUIRE(d.clearShapeImage(t).has_value());

  const std::string file = io::writePuzzle(d.puzzle());
  CHECK(file.find("<image id=\"0\" type=\"image/png\" width=\"40\" height=\"20\">" +
                  base64Encode(png) + "</image>") != std::string::npos);
  // the pictures come before the shapes that show them
  CHECK(file.find("<images>") < file.find("<shapes>"));
  CHECK(file.find("image/jpeg") == std::string::npos);
  CHECK(file.find("imageFit=\"stretch\" imageSrc=\"0 0 40 20\" imageBox=\"0 0 1 2\"") !=
        std::string::npos);

  Result<Puzzle> back = io::parsePuzzle(file);
  REQUIRE(back.has_value());
  CHECK(back->getShape(0).getImage() == shapeOf(d, s).getImage());
  REQUIRE(back->findImage(0) != nullptr);
  CHECK(*back->findImage(0)->bytes == png);
  CHECK(io::writePuzzle(*back) == file);
}

TEST_CASE("shape images: a picture placed by hand keeps its fit and part in the file",
          "[app][sliding][io]") {
  Document d(GridKind::Sliding);
  const Id s = piece(d, 2, 1, {{0, 0, 0}, {1, 0, 0}});
  REQUIRE(d.setShapeImage(s, {pngHeader(40, 20, 'm'), std::nullopt, ImageFit::Manual, {7, 3, 22, 11}})
              .has_value());
  CHECK(shapeOf(d, s).getImage().fit == ImageFit::Manual);
  const std::string file = io::writePuzzle(d.puzzle());
  CHECK(file.find("imageFit=\"manual\" imageSrc=\"7 3 22 11\"") != std::string::npos);
  Result<Puzzle> back = io::parsePuzzle(file);
  REQUIRE(back.has_value());
  CHECK(back->getShape(0).getImage() == shapeOf(d, s).getImage());
  CHECK(io::writePuzzle(*back) == file);
}

TEST_CASE("shape images: a stack of pictures, set in one step, kept by the file and turned",
          "[app][sliding][io]") {
  Document d(GridKind::Sliding);
  const Id s = piece(d, 3, 2, {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 1, 0}});
  // a picture smaller than the box (its part shown reaches past its
  // edges), and a second one on top of it
  const std::vector<Document::ImageLayer> two{
      {pngHeader(30, 20, 'a'), std::nullopt, ImageFit::Manual, {-30, -10, 90, 60}},
      {pngHeader(16, 16, 'b'), std::nullopt, ImageFit::Manual, {0, 0, 16, 16}}};
  roundTrip(d, [&](Document& doc) { return doc.setShapeImages(s, two); });
  const ShapeImages& stack = shapeOf(d, s).getImages();
  REQUIRE(stack.size() == 2);
  CHECK(stack[0].src == std::array<int, 4>{-30, -10, 90, 60});
  CHECK(stack[1].src == std::array<int, 4>{0, 0, 16, 16});
  CHECK(stack[0].box == stack[1].box);
  CHECK(d.puzzle().images().size() == 2);
  // the same stack again is no step; the pictures by id reorder it
  const auto before = d.revision();
  REQUIRE(d.setShapeImages(s, two).has_value());
  CHECK(d.revision() == before);
  const auto a = static_cast<unsigned int>(stack[0].id);
  const auto b = static_cast<unsigned int>(stack[1].id);
  roundTrip(d, [&](Document& doc) {
    return doc.setShapeImages(s, {{std::nullopt, b, ImageFit::Manual, {0, 0, 16, 16}},
                                  {std::nullopt, a, ImageFit::Manual, {-30, -10, 90, 60}}});
  });
  CHECK(shapeOf(d, s).getImages().at(0).id == static_cast<int>(b));

  // the file: the bottom one as before, the next as image2
  const std::string file = io::writePuzzle(d.puzzle());
  CHECK(file.find("imageSrc=\"0 0 16 16\"") != std::string::npos);
  CHECK(file.find("image2=\"" + std::to_string(a) + "\" image2Fit=\"manual\" image2Src=\"-30 -10 90 60\"") !=
        std::string::npos);
  Result<Puzzle> back = io::parsePuzzle(file);
  REQUIRE(back.has_value());
  CHECK(back->getShape(d.shapeIndex(s).value()).getImages() == shapeOf(d, s).getImages());
  CHECK(back->getShape(d.shapeIndex(s).value()).getExtraAttributes().empty());
  CHECK(io::writePuzzle(*back) == file);

  // a quarter turn turns every picture of the stack
  REQUIRE(d.shapeTask(s, ShapeTask::Rotate10).has_value());
  CHECK(shapeOf(d, s).getImages().at(0).turn != 0);
  CHECK(shapeOf(d, s).getImages().at(0).turn == shapeOf(d, s).getImages().at(1).turn);
  CHECK(shapeOf(d, s).getImages().at(0).box == shapeOf(d, s).getImages().at(1).box);

  // an empty stack takes them off; more than 16 are refused
  roundTrip(d, [&](Document& doc) { return doc.setShapeImages(s, {}); });
  CHECK(shapeOf(d, s).getImages().empty());
  const std::vector<Document::ImageLayer> many(
      kMaxShapeImages + 1, {std::nullopt, a, ImageFit::Manual, {0, 0, 30, 20}});
  refused(d, Errc::InvalidArgument, [&](Document& doc) { return doc.setShapeImages(s, many); });
}

TEST_CASE("shape images: a file's bad pictures are dropped, not refused", "[app][sliding][io]") {
  const std::string png = base64Encode(pngHeader(8, 8));
  auto file = [&](const std::string& images, const std::string& attrs) {
    return "<puzzle version=\"2\"><gridType type=\"5\"/><colors/>" + images +
           "<shapes><voxel x=\"1\" y=\"1\" z=\"1\" type=\"0\" " + attrs +
           ">#</voxel></shapes><problems/><comment/></puzzle>";
  };
  const std::string good = "image=\"3\" imageFit=\"crop\" imageSrc=\"0 0 8 8\" imageBox=\"0 0 1 1\" imageTurn=\"5\"";
  const std::string table = "<images><image id=\"3\" type=\"image/png\">" + png + "</image></images>";

  Result<Puzzle> ok = io::parsePuzzle(file(table, good));
  REQUIRE(ok.has_value());
  CHECK(ok->getShape(0).getImage().id == 3);
  CHECK(ok->getShape(0).getImage().turn == 5);
  CHECK(ok->findImage(3)->width == 8);

  // base64 wrapped over lines reads the same
  std::string wrapped = png;
  wrapped.insert(8, "\n  ");
  Result<Puzzle> wrap = io::parsePuzzle(
      file("<images><image id=\"3\" type=\"image/png\">" + wrapped + "</image></images>", good));
  REQUIRE(wrap.has_value());
  CHECK(wrap->getShape(0).getImage().id == 3);

  // no such image, bytes that are not the type said, a bad box or turn
  for (const auto& [images, attrs] : std::vector<std::pair<std::string, std::string>>{
           {"", good},
           {"<images><image id=\"3\" type=\"image/jpeg\">" + png + "</image></images>", good},
           {"<images><image id=\"3\" type=\"image/png\">@@@@</image></images>", good},
           {table, "image=\"3\" imageSrc=\"0 0 8 8\" imageBox=\"0 0 0 1\""},
           {table, "image=\"3\" imageSrc=\"0 0 8\" imageBox=\"0 0 1 1\""},
           {table, "image=\"3\" imageSrc=\"0 0 8 8\" imageBox=\"0 0 1 1\" imageTurn=\"8\""}}) {
    INFO(attrs);
    Result<Puzzle> p = io::parsePuzzle(file(images, attrs));
    REQUIRE(p.has_value());
    CHECK_FALSE(p->getShape(0).getImage().present());
    // the cells are read as ever
    CHECK(p->getShape(0).getState(0) == Voxel::kFilled);
  }
}

TEST_CASE("shape images: the mesh's faces looking up carry the picture's coordinates",
          "[app][sliding][mesh]") {
  Document d(GridKind::Sliding);
  const Id s = piece(d, 3, 3, {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 1, 0}});
  // without a picture, none
  CHECK(d.shapeMesh(s, mesh::MeshStyle::Flat).value().imageUv.empty());
  REQUIRE(d.setShapeImage(s, {pngHeader(30, 20), std::nullopt, ImageFit::Stretch, {0, 0, 30, 20}})
              .has_value());

  // (s, t) of every vertex looking up, in hundredths, sorted
  auto picture = [&d, s] {
    const mesh::DrawMesh m = d.shapeMesh(s, mesh::MeshStyle::Flat).value();
    REQUIRE(m.imageUv.size() == 2 * m.vertexCount());
    std::vector<std::pair<long, long>> out;
    for (std::size_t i = 0; i < m.vertexCount(); i++) {
      const float u = m.imageUv[2 * i];
      const float v = m.imageUv[2 * i + 1];
      if (m.normals[3 * i + 2] < 0.5F) {
        CHECK(u == -1.0F);
        CHECK(v == -1.0F);
        continue;
      }
      // the box is the L's 3 x 2: x / 3 across, 1 - y / 2 down
      CHECK(std::abs(u - m.positions[3 * i] / 3.0F) < 1e-5F);
      CHECK(std::abs(v - (1.0F - m.positions[3 * i + 1] / 2.0F)) < 1e-5F);
      out.emplace_back(std::lround(u * 100), std::lround(v * 100));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
  };
  const std::vector<std::pair<long, long>> upright = picture();
  CHECK_FALSE(upright.empty());
  CHECK(upright.front() == std::pair<long, long>{0, 0});

  // turned, the top faces show the same points of the picture
  REQUIRE(d.shapeTask(s, ShapeTask::Rotate10).has_value());
  const mesh::DrawMesh m = d.shapeMesh(s, mesh::MeshStyle::Flat).value();
  std::vector<std::pair<long, long>> turned;
  for (std::size_t i = 0; i < m.vertexCount(); i++) {
    if (m.normals[3 * i + 2] >= 0.5F) {
      turned.emplace_back(std::lround(m.imageUv[2 * i] * 100), std::lround(m.imageUv[2 * i + 1] * 100));
    }
  }
  std::sort(turned.begin(), turned.end());
  turned.erase(std::unique(turned.begin(), turned.end()), turned.end());
  CHECK(turned == upright);
}

TEST_CASE("shape images: a board takes a picture on its floor, kept by the file",
          "[app][sliding][io][mesh]") {
  Document d(GridKind::Sliding);
  const Id tray = d.addStartGoal(std::array<unsigned int, 3>{3, 2, 1}).value();
  const std::string png = pngHeader(30, 20, 'b');
  roundTrip(d, [&](Document& doc) {
    return doc.setShapeImage(tray, {png, std::nullopt, ImageFit::Stretch, {0, 0, 30, 20}});
  });
  // the box is the board's floor: every cell of a new board
  CHECK(shapeOf(d, tray).getImage().box == std::array<int, 4>{0, 0, 3, 2});

  // the floor's tops carry the picture, the corners at (0, 0) and (1, 1)
  const mesh::DrawMesh m = d.shapeMesh(tray, mesh::MeshStyle::Flat).value();
  REQUIRE(m.imageUv.size() == 2 * m.vertexCount());
  float lo = 2.0F;
  float hi = -1.0F;
  for (std::size_t i = 0; i < m.vertexCount(); i++) {
    if (m.normals[3 * i + 2] >= 0.5F) {
      lo = std::min({lo, m.imageUv[2 * i], m.imageUv[2 * i + 1]});
      hi = std::max({hi, m.imageUv[2 * i], m.imageUv[2 * i + 1]});
    }
  }
  CHECK(std::abs(lo) < 1e-5F);
  CHECK(std::abs(hi - 1.0F) < 1e-5F);

  Result<Puzzle> back = io::parsePuzzle(io::writePuzzle(d.puzzle()));
  REQUIRE(back.has_value());
  const Voxel& b = back->getShape(d.shapeIndex(tray).value());
  CHECK(sliding::isStartGoalShape(&b));
  CHECK(b.getImage() == shapeOf(d, tray).getImage());
  REQUIRE(back->findImage(b.getImage().id) != nullptr);
  CHECK(*back->findImage(b.getImage().id)->bytes == png);

  roundTrip(d, [&](Document& doc) { return doc.clearShapeImage(tray); });
  CHECK_FALSE(shapeOf(d, tray).getImage().present());
}

TEST_CASE("tray border: set on a board, undone, turned and kept by the file",
          "[app][sliding][io]") {
  Document d(GridKind::Sliding);
  const Id tray = d.addStartGoal(std::array<unsigned int, 3>{3, 2, 1}).value();
  const Id s = piece(d, 1, 1, {{0, 0, 0}});
  CHECK(shapeOf(d, tray).getBorder().isDefault());
  // a board without a border writes none of its attributes
  CHECK(io::writePuzzle(d.puzzle()).find("border") == std::string::npos);

  Document::BorderChange on;
  on.enabled = true;
  roundTrip(d, [&](Document& doc) { return doc.setTrayBorder(tray, on); });
  CHECK(shapeOf(d, tray).getBorder().enabled);
  // the same again is no step
  const auto before = d.revision();
  REQUIRE(d.setTrayBorder(tray, on).has_value());
  CHECK(d.revision() == before);

  Document::BorderChange look;
  look.corners = BorderCorners::Sharp;
  look.material = BorderMaterial::Wood;
  look.color = std::optional<std::uint32_t>{0x336699};
  look.thickness = 3;
  roundTrip(d, [&](Document& doc) { return doc.setTrayBorder(tray, look); });
  const TrayBorder& b = shapeOf(d, tray).getBorder();
  CHECK(b.enabled);
  CHECK(b.corners == BorderCorners::Sharp);
  CHECK(b.material == BorderMaterial::Wood);
  CHECK(b.color == 0x336699);
  CHECK(b.thickness == 3);

  // its picture: over the board's whole rectangle, another than the floor's
  const std::string png = pngHeader(40, 30, 'r');
  roundTrip(d, [&](Document& doc) {
    return doc.setShapeImage(tray, {png, std::nullopt, ImageFit::Stretch, {0, 0, 40, 30}, true});
  });
  CHECK_FALSE(shapeOf(d, tray).getImage().present());
  CHECK(shapeOf(d, tray).getBorder().images.at(0).box == std::array<int, 4>{0, 0, 3, 2});
  CHECK(d.puzzle().imageUsed(static_cast<unsigned int>(shapeOf(d, tray).getBorder().images.at(0).id)));

  // a quarter turn turns the border's picture with the board
  REQUIRE(d.shapeTask(tray, ShapeTask::Rotate10).has_value());
  CHECK(shapeOf(d, tray).getBorder().images.at(0).turn != 0);

  const std::string file = io::writePuzzle(d.puzzle());
  CHECK(file.find("border=\"1\" borderCorners=\"sharp\" borderMaterial=\"wood\" "
                  "borderColor=\"#336699\" borderThickness=\"3\" borderImage=\"0\" borderImageFit=\"stretch\" "
                  "borderImageSrc=\"0 0 40 30\"") != std::string::npos);
  Result<Puzzle> back = io::parsePuzzle(file);
  REQUIRE(back.has_value());
  CHECK(back->getShape(d.shapeIndex(tray).value()).getBorder() == shapeOf(d, tray).getBorder());
  REQUIRE(back->findImage(0) != nullptr);
  CHECK(*back->findImage(0)->bytes == png);
  CHECK(io::writePuzzle(*back) == file);

  // the color and the picture come off; the rest stays
  Document::BorderChange plain;
  plain.color = std::optional<std::uint32_t>{};
  roundTrip(d, [&](Document& doc) { return doc.setTrayBorder(tray, plain); });
  CHECK(shapeOf(d, tray).getBorder().color == -1);
  roundTrip(d, [&](Document& doc) { return doc.clearShapeImage(tray, true); });
  CHECK(shapeOf(d, tray).getBorder().images.empty());
  CHECK(shapeOf(d, tray).getBorder().material == BorderMaterial::Wood);

  // thin again; no thicker than kMaxBorderThickness
  Document::BorderChange thin;
  thin.thickness = 0;
  roundTrip(d, [&](Document& doc) { return doc.setTrayBorder(tray, thin); });
  CHECK(shapeOf(d, tray).getBorder().thickness == 0);
  CHECK(io::writePuzzle(d.puzzle()).find("borderThickness") == std::string::npos);
  Document::BorderChange thick;
  thick.thickness = kMaxBorderThickness + 1;
  refused(d, Errc::InvalidArgument, [&](Document& doc) { return doc.setTrayBorder(tray, thick); });

  // only a sliding puzzle's board has one
  refused(d, Errc::InvalidArgument, [&](Document& doc) { return doc.setTrayBorder(s, on); });
  refused(d, Errc::InvalidArgument, [&](Document& doc) {
    return doc.setShapeImage(s, {png, std::nullopt, ImageFit::Stretch, {0, 0, 40, 30}, true});
  });
  Document bricks(GridKind::Bricks);
  const Id c = bricks.createShape().value();
  refused(bricks, Errc::InvalidArgument,
          [&](Document& doc) { return doc.setTrayBorder(c, on); });
}
