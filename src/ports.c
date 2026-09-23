/* 端口/连接枚举：TCP、UDP，IPv4 与 IPv6，含占用进程 PID */
#include "portview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

typedef struct {
    PORT_ENTRY *items;
    size_t count;
    size_t cap;
} PORT_VEC;

typedef struct {
    DWORD pid;
    WCHAR name[MAX_PATH];
    WCHAR path[MAX_PATH];
} PROC_CACHE;

static PROC_CACHE *g_cache = NULL;
static size_t g_cacheN = 0;
static size_t g_cacheCap = 0;

static int VecPush(PORT_VEC *v, const PORT_ENTRY *e)
{
    if (v->count == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 256;
        PORT_ENTRY *p = (PORT_ENTRY *)realloc(v->items, nc * sizeof(PORT_ENTRY));
        if (!p) return 0;
        v->items = p;
        v->cap = nc;
    }
    v->items[v->count++] = *e;
    return 1;
}

const WCHAR *PortsStateText(DWORD state)
{
    switch (state) {
    case MIB_TCP_STATE_CLOSED:     return L"已关闭";
    case MIB_TCP_STATE_LISTEN:     return L"监听";
    case MIB_TCP_STATE_SYN_SENT:   return L"SYN 已发送";
    case MIB_TCP_STATE_SYN_RCVD:   return L"SYN 已接收";
    case MIB_TCP_STATE_ESTAB:      return L"已建立";
    case MIB_TCP_STATE_FIN_WAIT1:  return L"FIN 等待 1";
    case MIB_TCP_STATE_FIN_WAIT2:  return L"FIN 等待 2";
    case MIB_TCP_STATE_CLOSE_WAIT: return L"关闭等待";
    case MIB_TCP_STATE_CLOSING:    return L"正在关闭";
    case MIB_TCP_STATE_LAST_ACK:   return L"最后确认";
    case MIB_TCP_STATE_TIME_WAIT:  return L"时间等待";
    case MIB_TCP_STATE_DELETE_TCB: return L"已删除";
    default:                       return L"未知";
    }
}

static void FmtAddr(int family, void *addr, DWORD scope, WCHAR *buf, size_t cch)
{
    WCHAR tmp[64];
    tmp[0] = 0;

    InetNtopW(family, (PVOID)addr, tmp, 64);
    tmp[63] = 0;

    if (family == AF_INET6 && scope) {
        _snwprintf(buf, cch, L"%s%%%u", tmp, scope);
    } else {
        wcsncpy(buf, tmp, cch - 1);
    }
    buf[cch - 1] = 0;
}

/* ------------------------------------------------------------ 进程缓存 */

static void CacheReset(void)
{
    free(g_cache);
    g_cache = NULL;
    g_cacheN = 0;
    g_cacheCap = 0;
}

static const PROC_CACHE *CacheLookup(DWORD pid)
{
    size_t i;
    for (i = 0; i < g_cacheN; ++i) {
        if (g_cache[i].pid == pid) return &g_cache[i];
    }
    return NULL;
}

static void CacheStore(DWORD pid, const WCHAR *name, const WCHAR *path)
{
    PROC_CACHE c;
    if (g_cacheN == g_cacheCap) {
        size_t nc = g_cacheCap ? g_cacheCap * 2 : 64;
        PROC_CACHE *p = (PROC_CACHE *)realloc(g_cache, nc * sizeof(PROC_CACHE));
        if (!p) return;
        g_cache = p;
        g_cacheCap = nc;
    }
    c.pid = pid;
    wcsncpy(c.name, name, MAX_PATH - 1);
    c.name[MAX_PATH - 1] = 0;
    wcsncpy(c.path, path, MAX_PATH - 1);
    c.path[MAX_PATH - 1] = 0;
    g_cache[g_cacheN++] = c;
}

static void ResolveProc(PORT_ENTRY *e, const PROC_INFO *procs, size_t procCount)
{
    const PROC_CACHE *hit;
    WCHAR name[MAX_PATH], path[MAX_PATH];
    const PROC_INFO *pi;
    const WCHAR *base, *q;

    hit = CacheLookup(e->pid);
    if (hit) {
        wcsncpy(e->procName, hit->name, 63);
        e->procName[63] = 0;
        wcsncpy(e->procPath, hit->path, MAX_PATH - 1);
        e->procPath[MAX_PATH - 1] = 0;
        return;
    }

    name[0] = 0;
    path[0] = 0;

    if (e->pid == 0) {
        wcsncpy(name, L"System Idle Process", MAX_PATH - 1);
    } else if (e->pid == 4 && !ProcGetPath(4, path, MAX_PATH)) {
        wcsncpy(name, L"System", MAX_PATH - 1);
    } else {
        pi = ProcFind(procs, procCount, e->pid);
        if (pi) wcsncpy(name, pi->name, MAX_PATH - 1);
        ProcGetPath(e->pid, path, MAX_PATH);
        if (!name[0] && path[0]) {
            base = path;
            for (q = path; *q; ++q) {
                if (*q == L'\\' || *q == L'/') base = q + 1;
            }
            wcsncpy(name, base, MAX_PATH - 1);
        }
    }
    name[MAX_PATH - 1] = 0;
    path[MAX_PATH - 1] = 0;

    if (!name[0]) _snwprintf(name, MAX_PATH, L"PID %u", e->pid);

    CacheStore(e->pid, name, path);
    wcsncpy(e->procName, name, 63);
    e->procName[63] = 0;
    wcsncpy(e->procPath, path, MAX_PATH - 1);
    e->procPath[MAX_PATH - 1] = 0;
}

/* ------------------------------------------------------------ 各类表 */

static void AddTcp4(PORT_VEC *v, const PROC_INFO *procs, size_t procCount)
{
    PMIB_TCPTABLE_OWNER_PID table = NULL;
    DWORD size = 0, r, i;

    r = GetExtendedTcpTable(NULL, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (r != ERROR_INSUFFICIENT_BUFFER || size == 0) return;

    table = (PMIB_TCPTABLE_OWNER_PID)malloc(size);
    if (!table) return;

    r = GetExtendedTcpTable(table, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (r != NO_ERROR) { free(table); return; }

    for (i = 0; i < table->dwNumEntries; ++i) {
        MIB_TCPROW_OWNER_PID *row = &table->table[i];
        PORT_ENTRY e;
        IN_ADDR a;

        ZeroMemory(&e, sizeof(e));
        wcsncpy(e.proto, L"TCP", 7);

        a.S_un.S_addr = row->dwLocalAddr;
        FmtAddr(AF_INET, &a, 0, e.localAddr, 64);
        e.localPort = ntohs((u_short)row->dwLocalPort);

        a.S_un.S_addr = row->dwRemoteAddr;
        FmtAddr(AF_INET, &a, 0, e.remoteAddr, 64);
        e.remotePort = ntohs((u_short)row->dwRemotePort);

        wcsncpy(e.state, PortsStateText(row->dwState), 23);
        e.pid = row->dwOwningPid;
        ResolveProc(&e, procs, procCount);

        if (!VecPush(v, &e)) break;
    }
    free(table);
}

static void AddTcp6(PORT_VEC *v, const PROC_INFO *procs, size_t procCount)
{
    PMIB_TCP6TABLE_OWNER_PID table = NULL;
    DWORD size = 0, r, i;

    r = GetExtendedTcpTable(NULL, &size, TRUE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    if (r != ERROR_INSUFFICIENT_BUFFER || size == 0) return;

    table = (PMIB_TCP6TABLE_OWNER_PID)malloc(size);
    if (!table) return;

    r = GetExtendedTcpTable(table, &size, TRUE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    if (r != NO_ERROR) { free(table); return; }

    for (i = 0; i < table->dwNumEntries; ++i) {
        MIB_TCP6ROW_OWNER_PID *row = &table->table[i];
        PORT_ENTRY e;

        ZeroMemory(&e, sizeof(e));
        wcsncpy(e.proto, L"TCP6", 7);

        FmtAddr(AF_INET6, row->ucLocalAddr, row->dwLocalScopeId, e.localAddr, 64);
        e.localPort = ntohs((u_short)row->dwLocalPort);
        FmtAddr(AF_INET6, row->ucRemoteAddr, row->dwRemoteScopeId, e.remoteAddr, 64);
        e.remotePort = ntohs((u_short)row->dwRemotePort);

        wcsncpy(e.state, PortsStateText(row->dwState), 23);
        e.pid = row->dwOwningPid;
        ResolveProc(&e, procs, procCount);

        if (!VecPush(v, &e)) break;
    }
    free(table);
}

static void AddUdp4(PORT_VEC *v, const PROC_INFO *procs, size_t procCount)
{
    PMIB_UDPTABLE_OWNER_PID table = NULL;
    DWORD size = 0, r, i;

    r = GetExtendedUdpTable(NULL, &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (r != ERROR_INSUFFICIENT_BUFFER || size == 0) return;

    table = (PMIB_UDPTABLE_OWNER_PID)malloc(size);
    if (!table) return;

    r = GetExtendedUdpTable(table, &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (r != NO_ERROR) { free(table); return; }

    for (i = 0; i < table->dwNumEntries; ++i) {
        MIB_UDPROW_OWNER_PID *row = &table->table[i];
        PORT_ENTRY e;
        IN_ADDR a;

        ZeroMemory(&e, sizeof(e));
        wcsncpy(e.proto, L"UDP", 7);

        a.S_un.S_addr = row->dwLocalAddr;
        FmtAddr(AF_INET, &a, 0, e.localAddr, 64);
        e.localPort = ntohs((u_short)row->dwLocalPort);
        e.remoteAddr[0] = 0;
        e.remotePort = 0;
        e.state[0] = 0;

        e.pid = row->dwOwningPid;
        ResolveProc(&e, procs, procCount);

        if (!VecPush(v, &e)) break;
    }
    free(table);
}

static void AddUdp6(PORT_VEC *v, const PROC_INFO *procs, size_t procCount)
{
    PMIB_UDP6TABLE_OWNER_PID table = NULL;
    DWORD size = 0, r, i;

    r = GetExtendedUdpTable(NULL, &size, TRUE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    if (r != ERROR_INSUFFICIENT_BUFFER || size == 0) return;

    table = (PMIB_UDP6TABLE_OWNER_PID)malloc(size);
    if (!table) return;

    r = GetExtendedUdpTable(table, &size, TRUE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    if (r != NO_ERROR) { free(table); return; }

    for (i = 0; i < table->dwNumEntries; ++i) {
        MIB_UDP6ROW_OWNER_PID *row = &table->table[i];
        PORT_ENTRY e;

        ZeroMemory(&e, sizeof(e));
        wcsncpy(e.proto, L"UDP6", 7);

        FmtAddr(AF_INET6, &row->ucLocalAddr, row->dwLocalScopeId, e.localAddr, 64);
        e.localPort = ntohs((u_short)row->dwLocalPort);
        e.remoteAddr[0] = 0;
        e.remotePort = 0;
        e.state[0] = 0;

        e.pid = row->dwOwningPid;
        ResolveProc(&e, procs, procCount);

        if (!VecPush(v, &e)) break;
    }
    free(table);
}

void PortsFree(PORT_ENTRY *entries)
{
    free(entries);
}

int PortsEnumerate(PORT_ENTRY **entries, size_t *count)
{
    PORT_VEC v;
    PROC_INFO *procs = NULL;
    size_t procCount = 0;

    *entries = NULL;
    *count = 0;

    v.items = NULL;
    v.count = 0;
    v.cap = 0;

    ProcSnapshot(&procs, &procCount);
    CacheReset();

    AddTcp4(&v, procs, procCount);
    AddTcp6(&v, procs, procCount);
    AddUdp4(&v, procs, procCount);
    AddUdp6(&v, procs, procCount);

    ProcFree(procs);

    if (v.count == 0) {
        free(v.items);
        return 1; /* 成功，只是没有数据 */
    }

    *entries = v.items;
    *count = v.count;
    return 1;
}
