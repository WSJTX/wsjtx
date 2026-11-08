/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

int main(void)
{
  int count;
  LPWSTR *args = CommandLineToArgvW(GetCommandLineW(), &count);
  wchar_t marker[32768];
  HANDLE file;
  if (!args || count != 3)
    return 1;
  if (swprintf(marker, 32768, L"%ls\\launch-marker.txt", args[2]) < 0)
    return 1;
  LocalFree(args);
  file = CreateFileW(marker, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
  if (file == INVALID_HANDLE_VALUE)
    return 1;
  CloseHandle(file);
  return 0;
}
