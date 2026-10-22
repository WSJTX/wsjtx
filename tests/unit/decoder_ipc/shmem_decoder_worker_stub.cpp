// SPDX-License-Identifier: GPL-3.0-or-later
// Stands in for jt9 in test_shmem_decoder_backend: records its command line in
// <-a directory>/arguments, then copies the lines the test appends to
// <-a directory>/script to stdout until <-a directory>/stop appears or the
// shared segment named by -s reads SHUTDOWN, which it records in
// <-a directory>/exits. While <-a directory>/ignore-shutdown exists it plays a
// stuck worker and ignores SHUTDOWN.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "DecoderIpcProtocol.h"
#include "lib/NativeSharedMemory.hpp"
#include "lib/decoder_ipc_control.h"

int main (int argc, char ** argv)
{
#ifdef _WIN32
  _setmode (_fileno (stdout), _O_BINARY);
#endif
  std::string directory;
  std::string segment;
  for (int index = 1; index + 1 < argc; ++index)
    {
      if (std::string {argv[index]} == "-a") directory = argv[index + 1];
      if (std::string {argv[index]} == "-s") segment = argv[index + 1];
    }
  NativeSharedMemory shared;
  {
    std::ofstream arguments {directory + "/arguments", std::ios::binary};
    for (int index = 1; index < argc; ++index) arguments << argv[index] << '\n';
    auto const * stack = std::getenv ("OMP_STACKSIZE");
    arguments << "OMP_STACKSIZE=" << (stack ? stack : "") << '\n';
  }
  std::streamoff offset {0};
  for (int tick = 0; tick < 1200; ++tick)
    {
      if (std::ifstream {directory + "/stop"}) return 0;
      if (!shared.isAttached ()) shared.attach (segment);
      if (shared.isAttached () && !std::ifstream {directory + "/ignore-shutdown"}
          && DECODER_IPC_SHUTDOWN == decoder_ipc_atomic_load (
            &static_cast<decoder_ipc_control_t const *> (shared.data ())->state))
        {
          std::ofstream {directory + "/exits", std::ios::binary | std::ios::app} << "shutdown\n";
          return 0;
        }
      std::ifstream script {directory + "/script", std::ios::binary};
      if (script && script.seekg (offset))
        {
          std::string const pending {std::istreambuf_iterator<char> {script},
                                     std::istreambuf_iterator<char> {}};
          auto const end = pending.rfind ('\n');
          if (end != std::string::npos)
            {
              std::fwrite (pending.data (), 1, end + 1, stdout);
              std::fflush (stdout);
              offset += static_cast<std::streamoff> (end + 1);
            }
        }
      std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
  return 0;
}
