// inject.cpp - LoadLibrary injector for robrun.dll
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>

int main() {
    const wchar_t* proc = L"ROB RUN.exe";
    const char* dll = "robrun.dll";

    char full[MAX_PATH]{}; GetFullPathNameA(dll, MAX_PATH, full, nullptr);
    if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) { printf("robrun.dll not found next to inject.exe\n"); getchar(); return 1; }

    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{ sizeof(pe) }; DWORD pid = 0;
    if (Process32FirstW(s, &pe)) do { if (!_wcsicmp(pe.szExeFile, proc)) { pid = pe.th32ProcessID; break; } } while (Process32NextW(s, &pe));
    CloseHandle(s);
    if (!pid) { printf("ROB RUN.exe not running\n"); getchar(); return 1; }

    HANDLE p = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!p) { printf("OpenProcess failed (%lu) - run as admin\n", GetLastError()); getchar(); return 1; }

    size_t n = strlen(full) + 1;
    void* mem = VirtualAllocEx(p, nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) { printf("VirtualAllocEx failed (%lu)\n", GetLastError()); return 1; }
    WriteProcessMemory(p, mem, full, n, nullptr);

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    auto load = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryA");
    HANDLE t = CreateRemoteThread(p, nullptr, 0, load, mem, 0, nullptr);
    if (!t) { printf("CreateRemoteThread failed (%lu)\n", GetLastError()); return 1; }
    WaitForSingleObject(t, 8000);

    DWORD code = 0; GetExitCodeThread(t, &code);
    printf(code ? "injected (base 0x%lX)\n" : "LoadLibrary returned null - injection blocked\n", code);
    printf("press enter"); getchar();
    return code ? 0 : 2;
}