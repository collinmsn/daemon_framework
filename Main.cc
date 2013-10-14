#include <glog/logging.h>
#include <unistd.h>

int Main() {
  LOG(INFO) << "Main";
  // replace pause() with real main loop here
  pause();
  return 0;
}
