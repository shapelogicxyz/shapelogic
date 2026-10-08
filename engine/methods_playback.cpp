// SPDX-License-Identifier: GPL-3.0-or-later
// playback.* (api/playback.json): the samplers live in the engine behind
// handles, independent of the document once made.
#include "convert.h"
#include "engine.h"

#include <cstdint>

namespace bt::engine {

namespace {

// The most poses a frame holds (samples times pieces): 64 MiB of floats.
constexpr std::uint64_t kMaxPoses = std::uint64_t{1} << 21;

RpcResult<app::PlaybackSampler*> sampler(State& s, std::uint64_t handle) {
  const auto it = s.playbacks.find(handle);
  if (it == s.playbacks.end()) {
    return std::unexpected(toRpc(Error{Errc::NotFound, "there is no such playback"}));
  }
  return &it->second;
}

Reply playbackOpen(State& s, Params& p) {
  const app::Id problem = p.id("problem", IdKind::Problem);
  const app::Id solution = p.id("solution", IdKind::Solution);
  BTE_CHECK(p.finish());
  BTE_TRY(app::PlaybackSampler ps, s.document().playback(problem, solution));
  Json pieces = Json::array();
  for (const app::PlaybackPiece& piece : ps.pieces()) {
    pieces.push_back(Json{{"piece", piece.piece},
                          {"part", piece.part},
                          {"shape", shapeId(s, piece.shape)},
                          {"name", piece.name},
                          {"orientation", piece.orientation}});
  }
  const std::uint64_t handle = s.newHandle();
  Json j = Json::object();
  j["handle"] = formatId(IdKind::Playback, handle);
  j["steps"] = ps.stepCount();
  j["movesText"] = ps.movesText();
  j["pieces"] = std::move(pieces);
  s.playbacks.emplace(handle, std::move(ps));
  return j;
}

Reply playbackMesh(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::Playback);
  const unsigned int piece = p.count("piece");
  BTE_CHECK(p.finish());
  BTE_TRY(app::PlaybackSampler* ps, sampler(s, handle));
  if (piece >= ps->pieces().size()) {
    return coreError(Errc::InvalidArgument, "there is no such piece in the playback");
  }
  return meshReply(s, ps->pieceMesh(piece));
}

// The poses of every piece at every time, as a frame.
Json posesReply(State& s, const std::vector<float>& times, const char* timeName,
                const std::vector<std::vector<app::PiecePose>>& poses, std::size_t pieces) {
  std::vector<float> translation;
  std::vector<float> rotation;
  std::vector<std::uint8_t> visible;
  translation.reserve(poses.size() * pieces * 3);
  rotation.reserve(poses.size() * pieces * 4);
  visible.reserve(poses.size() * pieces);
  for (const std::vector<app::PiecePose>& at : poses) {
    for (const app::PiecePose& pose : at) {
      translation.insert(translation.end(), pose.translation.begin(), pose.translation.end());
      rotation.insert(rotation.end(), pose.rotation.begin(), pose.rotation.end());
      visible.push_back(pose.visible ? 1 : 0);
    }
  }
  FrameBuilder f;
  f.add(timeName, times);
  f.add("translation", translation);
  f.add("rotation", rotation);
  f.add("visible", visible);
  return Json{{"samples", poses.size()}, {"pieces", pieces}, {"frame", s.sendFrame(f)}};
}

Reply playbackPoses(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::Playback);
  const std::optional<std::int64_t> from = p.maybeInteger("from", 0, 0xFFFFFFFF);
  const std::optional<std::int64_t> to = p.maybeInteger("to", 0, 0xFFFFFFFF);
  const auto samples = static_cast<unsigned int>(p.optInteger("samplesPerStep", 8, 1, 256));
  BTE_CHECK(p.finish());
  BTE_TRY(app::PlaybackSampler* ps, sampler(s, handle));
  const auto first = static_cast<unsigned int>(from.value_or(0));
  const auto last = static_cast<unsigned int>(to.value_or(ps->stepCount()));
  if (first > last || last > ps->stepCount()) {
    return coreError(Errc::InvalidArgument, "the steps go from 0 to " +
                                                std::to_string(ps->stepCount()));
  }
  const std::uint64_t count = std::uint64_t{last - first} * samples + 1;
  if (count * ps->pieces().size() > kMaxPoses) {
    return coreError(Errc::InvalidArgument, "too many poses for one frame: ask for fewer steps");
  }
  std::vector<float> steps;
  std::vector<std::vector<app::PiecePose>> poses;
  for (std::uint64_t k = 0; k < count; k++) {
    const float step = static_cast<float>(first) + static_cast<float>(k) / static_cast<float>(samples);
    BTE_TRY(std::vector<app::PiecePose> at, ps->sample(step));
    steps.push_back(step);
    poses.push_back(std::move(at));
  }
  return posesReply(s, steps, "step", poses, ps->pieces().size());
}

Reply playbackTrack(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::Playback);
  app::TrackOptions o;
  o.samplesPerStep = static_cast<unsigned int>(p.optInteger("samplesPerStep", o.samplesPerStep, 1, 256));
  o.secondsPerStep = static_cast<float>(p.optNumber("secondsPerStep", static_cast<double>(o.secondsPerStep), 0.001, 3600));
  o.reassemble = p.optBoolean("reassemble", o.reassemble);
  BTE_CHECK(p.finish());
  BTE_TRY(app::PlaybackSampler* ps, sampler(s, handle));
  const std::uint64_t estimate =
      (std::uint64_t{ps->stepCount()} * o.samplesPerStep * 2 + 4) * ps->pieces().size();
  if (estimate > kMaxPoses) {
    return coreError(Errc::InvalidArgument, "too many poses for one frame: fewer samples per step");
  }
  BTE_TRY(const app::PlaybackTrack track, ps->track(o));
  return posesReply(s, track.times, "time", track.poses, ps->pieces().size());
}

Reply playbackClose(State& s, Params& p) {
  const std::uint64_t handle = p.id("handle", IdKind::Playback);
  BTE_CHECK(p.finish());
  if (s.playbacks.erase(handle) == 0) {
    return coreError(Errc::NotFound, "there is no such playback");
  }
  return Json::object();
}

}  // namespace

void addPlaybackMethods(MethodTable& t) {
  t["playback.open"] = playbackOpen;
  t["playback.mesh"] = playbackMesh;
  t["playback.poses"] = playbackPoses;
  t["playback.track"] = playbackTrack;
  t["playback.close"] = playbackClose;
}

}  // namespace bt::engine
