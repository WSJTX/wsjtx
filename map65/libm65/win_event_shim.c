// win_event_shim.c
#include <windows.h>
#include <stdlib.h>

void* win_create_event(void)
{
    HANDLE h = CreateEventW(NULL, FALSE, FALSE, NULL);
    return (void*)h;
}

int win_set_event(void* h)
{
    return SetEvent((HANDLE)h);
}

int win_reset_event(void* h)
{
    return ResetEvent((HANDLE)h);
}

int win_wait_for_single_object(void* h)
{
    DWORD rc = WaitForSingleObject((HANDLE)h, INFINITE);
    return (rc == WAIT_OBJECT_0) ? 0 : 1;
}

void win_close_event(void* h)
{
    CloseHandle((HANDLE)h);
}
