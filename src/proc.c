/* 进程相关：快照、路径、命令行、模块、结束进程、权限 */
#include "portview.h"

#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

typedef LONG(NTAPI *PFN_NTQIP)(HANDLE, LONG, PVOID, ULONG, PULONG);

typedef struct _PV_PBI {
    PVOID Reserved1;
    PVOID PebBaseAddress;
    PVOID Reserved2[2];
    ULONG_PTR UniqueProcessId;
    PVOID Reserved3;
} PV_PBI;

#define PV_ProcessBasicInformation 0
#define PV_ProcessWow64Information 26

/* PEB.ProcessParameters 偏移：x64 = 0x20，x86 = 0x10 */
/* RTL_USER_PROCESS_PARAMETERS.CommandLine 偏移：x64 = 0x70，x86 = 0x40 */

/* ---------------------------------------------------------------- 快照 */

int ProcSnapshot(PROC_INFO **out, size_t *outCount)
{
    HANDLE snap;
    PROCESSENTRY32W pe;
    PROC_INFO *list = NULL;
    size_t n = 0, cap = 256;

    *out = NULL;
    *outCount = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    list = (PROC_INFO *)malloc(cap * sizeof(PROC_INFO));
    if (!list) { CloseHandle(snap); return 0; }

    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            if (n == cap) {
                PROC_INFO *tmp;
                cap *= 2;
                tmp = (PROC_INFO *)realloc(list, cap * sizeof(PROC_INFO));
                if (!tmp) break;
                list = tmp;
            }
            list[n].pid = pe.th32ProcessID;
            wcsncpy(list[n].name, pe.szExeFile, MAX_PATH - 1);
            list[n].name[MAX_PATH - 1] = 0;
            n++;
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    *out = list;
    *outCount = n;
    return 1;
}

void ProcFree(PROC_INFO *list)
{
    free(list);
}

const PROC_INFO *ProcFind(const PROC_INFO *list, size_t count, DWORD pid)
{
    size_t i;
    if (!list) return NULL;
    for (i = 0; i < count; ++i) {
        if (list[i].pid == pid) return &list[i];
    }
    return NULL;
}

/* ------------------------------------------------------------ 进程路径 */

BOOL ProcGetPath(DWORD pid, WCHAR *buf, DWORD cch)
{
    HANDLE h;
    DWORD size = cch;
    BOOL ok;

    buf[0] = 0;
    if (pid == 0) return FALSE;

    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return FALSE;

    ok = QueryFullProcessImageNameW(h, 0, buf, &size);
    CloseHandle(h);

    if (!ok) buf[0] = 0;
    return ok;
}

/* ---------------------------------------------------------- 命令行读取 */

BOOL ProcGetCommandLine(DWORD pid, WCHAR *buf, DWORD cch)
{
    HANDLE h = NULL;
    PFN_NTQIP pNtQIP = NULL;
    PV_PBI pbi;
    PVOID peb = NULL;
    BOOL wow64 = FALSE, ret = FALSE;
    SIZE_T ptrSize, paramsOff, cmdOff, rd = 0;
    ULONG_PTR params = 0;
    USHORT cmdLen = 0;
    PVOID cmdPtr = NULL;

    buf[0] = 0;
    if (pid == 0 || pid == 4) return FALSE;

    pNtQIP = (PFN_NTQIP)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                       "NtQueryInformationProcess");
    if (!pNtQIP) return FALSE;

    h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return FALSE;

    ZeroMemory(&pbi, sizeof(pbi));
    if (pNtQIP(h, PV_ProcessBasicInformation, &pbi, (ULONG)sizeof(pbi), NULL) != 0 ||
        !pbi.PebBaseAddress) {
        CloseHandle(h);
        return FALSE;
    }

    {
        typedef BOOL (WINAPI *PFN_IW64)(HANDLE, PBOOL);
        PFN_IW64 pIsWow64 = (PFN_IW64)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                     "IsWow64Process");
        if (pIsWow64) pIsWow64(h, &wow64);
    }

    peb = pbi.PebBaseAddress;
    if (wow64) {
        ULONG_PTR peb32 = 0;
        if (pNtQIP(h, PV_ProcessWow64Information, &peb32, (ULONG)sizeof(peb32), NULL) == 0 &&
            peb32) {
            peb = (PVOID)peb32;
        }
    }

    ptrSize = wow64 ? 4 : 8;
    paramsOff = wow64 ? 0x10 : 0x20;
    cmdOff = wow64 ? 0x40 : 0x70;

    if (!ReadProcessMemory(h, (PBYTE)peb + paramsOff, &params, ptrSize, &rd) || !params) {
        CloseHandle(h);
        return FALSE;
    }

    if (!ReadProcessMemory(h, (PBYTE)params + cmdOff, &cmdLen, sizeof(cmdLen), &rd) ||
        !ReadProcessMemory(h, (PBYTE)params + cmdOff + ptrSize, &cmdPtr, ptrSize, &rd) ||
        !cmdPtr || cmdLen == 0) {
        CloseHandle(h);
        return FALSE;
    }

    if (cmdLen < (cch - 1) * sizeof(WCHAR)) {
        if (ReadProcessMemory(h, cmdPtr, buf, cmdLen, &rd) && rd >= sizeof(WCHAR)) {
            size_t chars = rd / sizeof(WCHAR);
            buf[chars] = 0;
            ret = TRUE;
        }
    }

    CloseHandle(h);
    return ret;
}

/* -------------------------------------------------------- 模块（DLL） */

int ProcEnumModules(DWORD pid, MODULE_INFO **out, size_t *outCount)
{
    HANDLE h;
    HMODULE *mods = NULL;
    DWORD cap = 256, needed = 0;
    MODULE_INFO *list = NULL;
    size_t n = 0, i;

    *out = NULL;
    *outCount = 0;

    h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return 0;

    for (;;) {
        mods = (HMODULE *)malloc(cap * sizeof(HMODULE));
        if (!mods) { CloseHandle(h); return 0; }
        needed = 0;
        if (!EnumProcessModulesEx(h, mods, cap * sizeof(HMODULE), &needed, LIST_MODULES_ALL)) {
            free(mods);
            CloseHandle(h);
            return 0;
        }
        if (needed <= cap * sizeof(HMODULE)) break;
        free(mods);
        cap = needed / sizeof(HMODULE) + 16;
    }

    n = needed / sizeof(HMODULE);
    list = (MODULE_INFO *)calloc(n ? n : 1, sizeof(MODULE_INFO));
    if (!list) { free(mods); CloseHandle(h); return 0; }

    for (i = 0; i < n; ++i) {
        MODULEINFO mi;
        GetModuleFileNameExW(h, mods[i], list[i].path, MAX_PATH);
        ZeroMemory(&mi, sizeof(mi));
        if (GetModuleInformation(h, mods[i], &mi, sizeof(mi))) {
            list[i].base = (ULONGLONG)(ULONG_PTR)mi.lpBaseOfDll;
            list[i].size = mi.SizeOfImage;
        }
    }

    free(mods);
    CloseHandle(h);
    *out = list;
    *outCount = n;
    return 1;
}

void ProcFreeModules(MODULE_INFO *list)
{
    free(list);
}

/* ------------------------------------------------------------ 结束进程 */

BOOL ProcTerminate(DWORD pid)
{
    HANDLE h;
    BOOL ok;

    if (pid == 0 || pid == 4) return FALSE;

    h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) return FALSE;

    ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok;
}

typedef struct { DWORD pid; DWORD parent; } PP;

static void CollectTree(const PP *arr, size_t n, DWORD parent,
                        DWORD *out, size_t *cnt, size_t cap)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (arr[i].parent != parent || arr[i].pid == 0) continue;
        if (*cnt >= cap) return;
        out[*cnt] = arr[i].pid;
        (*cnt)++;
        CollectTree(arr, n, arr[i].pid, out, cnt, cap);
    }
}

BOOL ProcTerminateTree(DWORD pid)
{
    HANDLE snap;
    PROCESSENTRY32W pe;
    PP *arr = NULL;
    size_t n = 0, cap = 256;
    DWORD *targets = NULL;
    size_t cnt = 0, i;
    BOOL ok = TRUE;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return ProcTerminate(pid);

    arr = (PP *)malloc(cap * sizeof(PP));
    if (!arr) { CloseHandle(snap); return ProcTerminate(pid); }

    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (n == cap) {
                PP *tmp;
                cap *= 2;
                tmp = (PP *)realloc(arr, cap * sizeof(PP));
                if (!tmp) break;
                arr = tmp;
            }
            arr[n].pid = pe.th32ProcessID;
            arr[n].parent = pe.th32ParentProcessID;
            n++;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    targets = (DWORD *)malloc((n + 1) * sizeof(DWORD));
    if (!targets) { free(arr); return ProcTerminate(pid); }

    targets[cnt++] = pid;
    CollectTree(arr, n, pid, targets, &cnt, n + 1);

    /* 先结束子进程，再结束父进程 */
    for (i = cnt; i > 0; --i) {
        if (!ProcTerminate(targets[i - 1])) ok = FALSE;
    }

    free(targets);
    free(arr);
    return ok;
}

/* ------------------------------------------------------------ 文件定位 */

BOOL ProcOpenFileLocation(HWND hwnd, const WCHAR *path)
{
    WCHAR *arg;
    size_t len;
    HINSTANCE r;

    if (!path || !*path) return FALSE;

    /* 按实际长度分配，避免长路径（\\?\ 前缀，可超过 MAX_PATH）被截断成无效路径 */
    len = wcslen(path) + 16;
    arg = (WCHAR *)malloc(len * sizeof(WCHAR));
    if (!arg) return FALSE;

    wcscpy(arg, L"/select,\"");
    wcscat(arg, path);
    wcscat(arg, L"\"");

    r = ShellExecuteW(hwnd, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
    free(arg);
    return (INT_PTR)r > 32;
}

/* -------------------------------------------------------------- 权限 */

BOOL ProcIsElevated(void)
{
    BOOL elevated = FALSE;
    HANDLE tok = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID adminSid = NULL;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION te;
        DWORD len = 0;
        if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &len)) {
            elevated = te.TokenIsElevated;
        } else {
            elevated = TRUE; /* 无 UAC 的系统 */
        }
        CloseHandle(tok);
    }

    if (!elevated &&
        AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminSid)) {
        BOOL member = FALSE;
        if (CheckTokenMembership(NULL, adminSid, &member) && member) elevated = TRUE;
        FreeSid(adminSid);
    }

    return elevated;
}

BOOL ProcElevate(HWND hwnd)
{
    WCHAR exe[MAX_PATH];
    HINSTANCE r;

    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return FALSE;

    r = ShellExecuteW(hwnd, L"runas", exe, NULL, NULL, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

BOOL ProcEnableDebugPriv(void)
{
    HANDLE tok = NULL;
    TOKEN_PRIVILEGES tp;
    BOOL ok = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        return FALSE;
    }

    ZeroMemory(&tp, sizeof(tp));
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid)) {
        ok = AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), NULL, NULL) &&
             GetLastError() != ERROR_NOT_ALL_ASSIGNED;
    }

    CloseHandle(tok);
    return ok;
}
