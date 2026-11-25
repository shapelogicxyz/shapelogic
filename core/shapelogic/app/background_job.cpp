// SPDX-License-Identifier: GPL-3.0-or-later
// New code (cleanup item 11.12): the skeleton Analysis and PlacementFit
// shared by hand.
#include "shapelogic/app/background_job.h"

#include "shapelogic/base/assert.h"

#include <exception>

namespace bt::app {

BackgroundJob::~BackgroundJob() {
  BT_ASSERT_MSG(!thread_.joinable(), "a job's destructor must finish() its thread");
}

void BackgroundJob::stop() {
  stop_.request(StopKind::Abort);
  onStop();
}

void BackgroundJob::wait() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void BackgroundJob::start() {
  thread_ = std::thread([this] {
    try {
      run();
    } catch (const std::exception& e) {
      const std::lock_guard<std::mutex> lock(mutex_);
      failure_ = e.what();
    }
    active_.store(false, std::memory_order_release);
  });
}

void BackgroundJob::finish() {
  stop();
  wait();
}

}  // namespace bt::app
