/* SPDX-License-Identifier: GPL-3.0-or-later */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shldisp.h>

static HRESULT launch_from_desktop (wchar_t const * file, wchar_t const * directory)
{
  IShellWindows * windows = NULL;
  IDispatch * desktop = NULL;
  IServiceProvider * provider = NULL;
  IShellBrowser * browser = NULL;
  IShellView * view = NULL;
  IDispatch * background = NULL;
  IShellFolderViewDual * folder = NULL;
  IDispatch * application = NULL;
  IShellDispatch2 * shell = NULL;
  VARIANT location = {0}, empty = {0}, working_directory = {0}, show = {0};
  BSTR executable = NULL;
  long window;
  HRESULT result;
  if (!GetShellWindow ()) return HRESULT_FROM_WIN32 (ERROR_NOT_FOUND);

  /* Obtain the existing desktop's automation object so alternate administrator
     credentials cannot select the administrator's user profile.
     https://devblogs.microsoft.com/oldnewthing/20131118-00/?p=2643 */
  result = CoCreateInstance (&CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER,
                             &IID_IShellWindows, (void **) &windows);
  if (FAILED (result)) goto cleanup;
  V_VT (&location) = VT_I4;
  V_I4 (&location) = CSIDL_DESKTOP;
  result = IShellWindows_FindWindowSW (windows, &location, &empty, SWC_DESKTOP,
                                      &window, SWFO_NEEDDISPATCH, &desktop);
  if (result != S_OK || !desktop)
    {
      result = HRESULT_FROM_WIN32 (ERROR_NOT_FOUND);
      goto cleanup;
    }
  result = IDispatch_QueryInterface (desktop, &IID_IServiceProvider, (void **) &provider);
  if (FAILED (result)) goto cleanup;
  result = IServiceProvider_QueryService (provider, &SID_STopLevelBrowser,
                                         &IID_IShellBrowser, (void **) &browser);
  if (FAILED (result)) goto cleanup;
  result = IShellBrowser_QueryActiveShellView (browser, &view);
  if (FAILED (result)) goto cleanup;
  result = IShellView_GetItemObject (view, SVGIO_BACKGROUND, &IID_IDispatch,
                                   (void **) &background);
  if (FAILED (result)) goto cleanup;
  result = IDispatch_QueryInterface (background, &IID_IShellFolderViewDual, (void **) &folder);
  if (FAILED (result)) goto cleanup;
  result = IShellFolderViewDual_get_Application (folder, &application);
  if (FAILED (result)) goto cleanup;
  result = IDispatch_QueryInterface (application, &IID_IShellDispatch2, (void **) &shell);
  if (FAILED (result)) goto cleanup;

  executable = SysAllocString (file);
  V_VT (&working_directory) = VT_BSTR;
  V_BSTR (&working_directory) = SysAllocString (directory);
  if (!executable || !V_BSTR (&working_directory))
    {
      result = E_OUTOFMEMORY;
      goto cleanup;
    }
  V_VT (&show) = VT_I4;
  V_I4 (&show) = SW_SHOWNORMAL;
  result = IShellDispatch2_ShellExecute (shell, executable, empty, working_directory, empty, show);

cleanup:
  SysFreeString (executable);
  VariantClear (&working_directory);
  if (shell) IShellDispatch2_Release (shell);
  if (application) IDispatch_Release (application);
  if (folder) IShellFolderViewDual_Release (folder);
  if (background) IDispatch_Release (background);
  if (view) IShellView_Release (view);
  if (browser) IShellBrowser_Release (browser);
  if (provider) IServiceProvider_Release (provider);
  if (desktop) IDispatch_Release (desktop);
  if (windows) IShellWindows_Release (windows);
  return result;
}

int WINAPI WinMain (HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
  int count;
  wchar_t ** arguments = CommandLineToArgvW (GetCommandLineW (), &count);
  HRESULT result = E_INVALIDARG;
  (void) instance;
  (void) previous;
  (void) command_line;
  (void) show;

  if (arguments && count == 3)
    {
      result = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);
      if (SUCCEEDED (result))
        {
          result = launch_from_desktop (arguments[1], arguments[2]);
          CoUninitialize ();
        }
    }
  LocalFree (arguments);
  return (int) result;
}
