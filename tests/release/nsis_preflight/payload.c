/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>

#ifdef FIXTURE_DLL
__declspec(dllexport) int fixture_version(void)
{
  return FIXTURE_VERSION;
}
#else
int main(void)
{
  Sleep(INFINITE);
  return FIXTURE_VERSION;
}
#endif
