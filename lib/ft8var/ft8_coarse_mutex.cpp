#include <mutex>

namespace {
std::mutex coarse_mutex;
}

extern "C" void wsjt_ft8_coarse_lock() {
  coarse_mutex.lock();
}

extern "C" void wsjt_ft8_coarse_unlock() {
  coarse_mutex.unlock();
}
