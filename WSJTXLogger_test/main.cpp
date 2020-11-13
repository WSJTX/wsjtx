#include <cstdlib>
#include "Logger.hpp"
#include "WSJTXLogging.hpp"

int main(int /*argc*/, char */*argv*/[])
{
  WSJTXLogging lg;
  LOG_INFO ("Program start up");
  LOG_INFO ("Program close down");
  return EXIT_SUCCESS;
}
