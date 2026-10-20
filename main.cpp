#include "utils_log/logger.hpp"
#include <cstdlib>
#include <string_view>
#include <thread>
#include <vector>

static void inner() {
  LOG_START;
  LOG_MSG << "inner work";
}

static void nested(bool crash) {
  LOG_START1("nested");
  LOG_HERE("checkpoint");
  inner();
  if (crash) std::abort();
}

// Crash with only the outermost scope open and nothing logged after its start.
// This is the case the |0 off-by-one currently misses.
static void outerOnly() {
  LOG_START;
  std::abort();
}

int main(int argc, char **argv) {
  const std::string_view mode = argc > 1 ? argv[1] : "";

  SET_LOG_OUTPUT_FILE_PATH("test_output.log");
  SET_LOG_DIAGNOSTICS_FILE_PATH("test_diagnostics.log");

  LOG_MSG << "hello" << 42 << 3.14 << std::string("str") << std::string_view("sv");
  LOG_MSG << "no" << LOG_NOSPACE << "space" << LOG_SPACE << "back";
  LOG_MSGNF << "console only";

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
    threads.emplace_back([i] {
      for (int j = 0; j < 100; ++j) LOG_MSG << "thread" << i << "msg" << j;
    });
  for (auto &t : threads) t.join();

  if (mode == "crash-outer") outerOnly();
  nested(mode == "crash-nested");

  LOG_MSG_WARN << "warn";
  LOG_MSG_ERROR << "err";
  LOG_MSG_DEBUG << "should be hidden";
  SET_LOG_MIN_LEVEL(utils_log::Level::debug);
  LOG_MSG_DEBUG << "now visible";

  // runtime rotation: ~10 MB should leave test_output.log.old and a fresh test_output.log
  const std::string pad(100, 'x');
  for (int i = 0; i < 100000; ++i) LOG_MSG << pad << i;

  // dangling-view check: name long enough to leave SSO, run with -DSANITIZE=address
  LOG_START1("a_name_longer_than_the_sso_buffer");

  utils_log::Log::terminate();
  return 0;
}
