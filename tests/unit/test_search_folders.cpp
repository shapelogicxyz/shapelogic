// SPDX-License-Identifier: GPL-3.0-or-later
// Every run of the unit tests gets search folders of its own: the engine's
// defaults follow SHAPELOGIC_SLIDE_DIR and SHAPELOGIC_PANEX_DIR, which
// would otherwise be the user's cache folder, shared by every shard and
// suite running side by side. Two solves of one problem cannot work in the
// same folders (decision 0035), and the tests leave nothing in the cache.
#include "scratch_dir.h"

#include <catch2/catch_test_run_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdlib>
#include <optional>
#include <string>

namespace {

void setVariable(const char* name, const std::string& value) {
#if defined(_WIN32)
  (void)_putenv_s(name, value.c_str());
#else
  (void)setenv(name, value.c_str(), 1);  // NOLINT(concurrency-mt-unsafe): before any test runs
#endif
}

class SearchFolders : public Catch::EventListenerBase {
 public:
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(const Catch::TestRunInfo& /*info*/) override {
    root_.emplace("folders");
    setVariable("SHAPELOGIC_SLIDE_DIR", (root_->path() / "sliding").string());
    setVariable("SHAPELOGIC_PANEX_DIR", (root_->path() / "panex").string());
  }

  void testRunEnded(const Catch::TestRunStats& /*stats*/) override { root_.reset(); }

 private:
  std::optional<bt::test::ScratchDir> root_;
};

}  // namespace

CATCH_REGISTER_LISTENER(SearchFolders)
