#include <mutex>

namespace {
std::mutex request_mutex;
}

extern "C" void lock_decode_requests()
{
  request_mutex.lock();
}

extern "C" void unlock_decode_requests()
{
  request_mutex.unlock();
}
