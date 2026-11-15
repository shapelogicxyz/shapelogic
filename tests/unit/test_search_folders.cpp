// SPDX-License-Identifier: GPL-3.0-or-later
// Every run of the unit tests gets search folders of its own: the engine's
// defaults follow SHAPELOGIC_SLIDE_DIR and SHAPELOGIC_PANEX_DIR, which
// would otherwise be the user's cache folder, shared by every shard and
// suite running side by side. Two solves of one problem cannot work in the
// same folders (decision 0035), and the tests leave nothing in the cache.
#include <catch2/catch_test_run_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

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
    std::random_device rd;
    root_ = std::filesystem::temp_directory_path() /
            ("bt_test_folders_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    setVariable("SHAPELOGIC_SLIDE_DIR", (root_ / "sliding").string());
    setVariable("SHAPELOGIC_PANEX_DIR", (root_ / "panex").string());
  }

  void testRunEnded(const Catch::TestRunStats& /*stats*/) override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

 private:
  std::filesystem::path root_;
};

}  // namespace

CATCH_REGISTER_LISTENER(SearchFolders)
