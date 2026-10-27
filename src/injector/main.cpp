// main.cpp -- copy wxhook.dll to %TEMP%, write the .home sidecar, inject it.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <set>
#include <string>
#include <utility>
#include <vector>

static std::string u8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static bool namedEventExists(const char* pattern, DWORD pid) {
    char name[64] = {0};
    _snprintf(name, sizeof name, pattern, (unsigned long)pid);
    name[sizeof name - 1] = 0;
    HANDLE h = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    if (!h) return false;
    CloseHandle(h);
    return true;
}

static bool hasModule(DWORD pid, const wchar_t* prefix) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    const size_t n = wcslen(prefix);
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsnicmp(me.szModule, prefix, n) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

static bool portInUse(int port) {
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    bool used = false;
    int r = connect(s, (sockaddr*)&a, sizeof a);
    if (r == 0) {
        used = true;
    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set wf, ef;
        FD_ZERO(&wf);
        FD_ZERO(&ef);
        FD_SET(s, &wf);
        FD_SET(s, &ef);
        timeval tv{0, 300000};
        if (select(0, nullptr, &wf, &ef, &tv) > 0) {
            int err = 0, el = sizeof err;
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &el) == 0 && err == 0)
                used = true;
        }
    }
    closesocket(s);
    WSACleanup();
    return used;
}

static std::set<DWORD> g_wndPids;

static BOOL CALLBACK enumWnd(HWND h, LPARAM) {
    if (!IsWindowVisible(h)) return TRUE;
    if (GetWindowTextLengthA(h) <= 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid) g_wndPids.insert(pid);
    return TRUE;
}

static SIZE_T workingSet(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return 0;
    PROCESS_MEMORY_COUNTERS pmc{};
    SIZE_T ws = 0;
    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) ws = pmc.WorkingSetSize;
    CloseHandle(h);
    return ws;
}

static std::wstring remoteCmdLine(DWORD pid) {
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return std::wstring();
    typedef LONG(NTAPI* NtQIP_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static NtQIP_t q = (NtQIP_t)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                      "NtQueryInformationProcess");
    std::wstring r;
    if (q) {
        ULONG len = 0;
        q(hp, 60, nullptr, 0, &len);
        if (len >= 16 && len < 32768) {
            std::vector<char> buf(len + 16, 0);
            ULONG got = 0;
            if (q(hp, 60, buf.data(), (ULONG)buf.size(), &got) >= 0) {
                USHORT wl = *(USHORT*)buf.data();
                PWSTR wb = *(PWSTR*)(buf.data() + 8);
                if (wb && wl >= 2 && wl < 16000) r.assign(wb, wl / 2);
            }
        }
    }
    CloseHandle(hp);
    return r;
}

static void findMainPids(std::vector<DWORD>* out) {
    out->clear();
    g_wndPids.clear();
    EnumWindows(enumWnd, 0);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    std::vector<std::pair<SIZE_T, DWORD>> cand;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"Weixin.exe") != 0) continue;
            DWORD pid = pe.th32ProcessID;
            std::wstring cmd = remoteCmdLine(pid);
            if (!cmd.empty()) {
                if (cmd.find(L"--type=") != std::wstring::npos) continue;
            } else if (g_wndPids.count(pid) == 0) {
                continue;
            }
            SIZE_T key = (g_wndPids.count(pid) > 0) ? (SIZE_T)-1 : workingSet(pid);
            cand.push_back(std::make_pair(key, pid));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    std::sort(cand.begin(), cand.end(),
              [](const std::pair<SIZE_T, DWORD>& a, const std::pair<SIZE_T, DWORD>& b) {
                  return a.first > b.first;
              });
    for (auto& entry : cand) out->push_back(entry.second);
}

static std::wstring exeDir() {
    wchar_t p[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    if (!n) return L".";
    std::wstring s(p, n);
    size_t k = s.find_last_of(L"\\/");
    return (k == std::wstring::npos) ? L"." : s.substr(0, k);
}

static std::wstring sidecarOf(const std::wstring& file) {
    std::wstring s = file;
    size_t d = s.find_last_of(L'.');
    size_t k = s.find_last_of(L"\\/");
    if (d != std::wstring::npos && (k == std::wstring::npos || d > k)) s.resize(d);
    return s + L".home";
}

static bool writeSidecar(const std::wstring& dllCopy, const std::wstring& home) {
    const std::string text = u8(home);
    HANDLE h = CreateFileW(sidecarOf(dllCopy).c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr) &&
              written == text.size();
    CloseHandle(h);
    return ok;
}

int main() {
    const std::wstring dir = exeDir();
    const std::wstring src = dir + L"\\wxhook.dll";
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
        printf("wxhook: wxhook.dll not found next to the injector: %s\n", u8(src).c_str());
        return 1;
    }

    std::vector<DWORD> pids;
    findMainPids(&pids);
    if (pids.empty()) {
        printf("wxhook: main Weixin.exe not found\n");
        return 1;
    }
    const DWORD pid = pids.front();
    printf("wxhook: main Weixin pid=%lu\n", (unsigned long)pid);
    if (pids.size() > 1)
        printf("wxhook: %zu main instances found; using pid %lu\n", pids.size(),
               (unsigned long)pid);

    if (namedEventExists("Local\\wxhook_inst_%lu", pid) || hasModule(pid, L"wxhook")) {
        printf("wxhook: already injected into pid %lu\n", (unsigned long)pid);
        return 0;
    }
    if (portInUse(30002)) {
        printf("wxhook: port 30002 is already serving a sender; restart WeChat before injecting\n");
        return 1;
    }

    wchar_t temp[MAX_PATH] = {0};
    if (!GetTempPathW(MAX_PATH, temp)) {
        printf("wxhook: GetTempPathW failed\n");
        return 1;
    }
    wchar_t name[64] = {0};
    _snwprintf(name, 64, L"wxhook_%lu_%lu.dll", (unsigned long)pid,
               (unsigned long)GetTickCount64());
    name[63] = 0;
    const std::wstring copy = std::wstring(temp) + name;

    if (!CopyFileW(src.c_str(), copy.c_str(), FALSE)) {
        printf("wxhook: CopyFileW failed, err=%lu\n", (unsigned long)GetLastError());
        return 1;
    }
    writeSidecar(copy, dir);

    HANDLE proc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!proc) {
        DWORD err = GetLastError();
        printf("wxhook: OpenProcess failed, err=%lu%s\n", (unsigned long)err,
               err == 5 ? " (run as administrator)" : "");
        return 1;
    }

    const SIZE_T bytes = (copy.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (!remote) {
        printf("wxhook: VirtualAllocEx failed, err=%lu\n", (unsigned long)GetLastError());
        CloseHandle(proc);
        return 1;
    }
    if (!WriteProcessMemory(proc, remote, copy.c_str(), bytes, nullptr)) {
        printf("wxhook: WriteProcessMemory failed, err=%lu\n", (unsigned long)GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return 1;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    if (!loadLibrary) {
        printf("wxhook: LoadLibraryW not found\n");
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return 1;
    }

    HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (!thread) {
        printf("wxhook: CreateRemoteThread failed, err=%lu\n", (unsigned long)GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return 1;
    }

    DWORD wait = WaitForSingleObject(thread, 20000);
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);

    if (wait != WAIT_OBJECT_0) {
        printf("wxhook: injection timed out\n");
        return 1;
    }
    if (!exitCode) {
        printf("wxhook: LoadLibraryW failed in the target process\n");
        return 1;
    }
    printf("wxhook: injected %s into pid %lu\n", u8(copy).c_str(), (unsigned long)pid);
    printf("wxhook: API base http://127.0.0.1:30002\n");
    return 0;
}
