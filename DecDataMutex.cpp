#include "DecDataMutex.hpp"

#include <atomic>
#include <QMutex>

namespace
{
std::atomic_bool& input_blocked ()
{
  static std::atomic_bool blocked {false};
  return blocked;
}
}

QMutex& dec_data_mutex()
{
  static QMutex mutex;
  return mutex;
}

bool dec_data_input_blocked ()
{
  return input_blocked ().load (std::memory_order_acquire);
}

void set_dec_data_input_blocked (bool blocked)
{
  input_blocked ().store (blocked, std::memory_order_release);
}
