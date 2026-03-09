#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    char exePath[MAX_PATH];
    char configName[] = "JTTY2";
    char windowHandle[64] = "";
    char commandLine[MAX_PATH + 256];
    
    // Get the directory of the current executable
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0) {
        printf("Error getting executable path.\n");
        return 1;
    }
    
    // Replace the executable name with wsjtx.exe
    char *lastSlash = strrchr(exePath, '\\');
    if (lastSlash != NULL) {
        strcpy(lastSlash + 1, "wsjtx.exe");
    } else {
        printf("Error parsing executable path.\n");
        return 1;
    }
    
    // Parse arguments (-h<handle> or -h <handle>)
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-h", 2) == 0) {
            if (strlen(argv[i]) > 2) {
                // -h0DED
                strncpy(windowHandle, argv[i] + 2, sizeof(windowHandle) - 1);
            } else if (i + 1 < argc) {
                // -h 0DED
                strncpy(windowHandle, argv[i + 1], sizeof(windowHandle) - 1);
                i++; // Skip the next arg
            }
        }
    }
    
    // Build the command line
    if (strlen(windowHandle) > 0) {
        snprintf(commandLine, sizeof(commandLine), "\"%s\" --config \"%s\" --window-handle \"%s\"", 
                 exePath, configName, windowHandle);
    } else {
        snprintf(commandLine, sizeof(commandLine), "\"%s\" --config \"%s\"", 
                 exePath, configName);
    }
    
    // Initialize STARTUPINFO and PROCESS_INFORMATION
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    // Create the process
    if (!CreateProcessA(NULL, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("Failed to start wsjtx.exe. Error code: %lu\n", GetLastError());
        return 1;
    }
    
    // Close process and thread handles
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return 0;
}
