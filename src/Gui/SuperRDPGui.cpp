// SuperRDPGui.cpp - GUI front-end for SuperRDP (anhkgg) console installer.
// Replicates SuperRDP2 (closed-source Delphi) GUI feature set with pure Win32 API:
//   - Install / Uninstall / Force restart Terminal Services
//   - Sync latest rdpwrap.ini from online sources
//   - AutoSupport: analyze local termsrv.dll by signature scan and generate ini section
//   - Boot-time auto support (HKLM Run -> /silent)
//   - Log window streaming console installer output
// Build: MinGW-w64 g++ (x64 only)
//   x64:  g++ -O2 -municode -mwindows ... SuperRDPGui.cpp gui_res.o manifest.o -lshlwapi -lurlmon -lversion -lcomctl32
// Author of GUI: rebuild project (2026). Console core: anhkgg (Apache-2.0).

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN   // keep windows.h from pulling in old winsock.h
#endif
#include <windows.h>
#include <winsvc.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <urlmon.h>
#include <winver.h>
#include <commctrl.h>
#include <wtsapi32.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>   // LLONG_MAX（SLInit 候选段距离计算）
#include <string>
#include <vector>

#pragma comment(lib, "urlmon.lib")  // (msvc hint; mingw links via -l)
#pragma comment(lib, "wtsapi32.lib")

// ---------------- IDs ----------------
// Buttons (1:1 replica of original SuperRDP2 by 汉客儿)
#define ID_BTN_INSTALL    1001
#define ID_BTN_UNINSTALL  1002
#define ID_BTN_STARTSVC   1003
#define ID_BTN_STOPSVc    1004
#define ID_BTN_SYNC       1005
#define ID_BTN_UPDATE     1006

#define ID_BTN_AUTOA      1010   // 自动分析 toggle
#define ID_BTN_AUTOB      1011   // 开机启动 toggle
#define ID_PANEL          1012   // big background container (overlaps children)
#define ID_BTN_PLUS       1013
#define ID_BTN_RESTART    1014   // (legacy worker action; no UI button in 1:1 layout)
#define ID_BTN_STATUS     1015   // (legacy worker action; no UI button in 1:1 layout)
// Static labels / values
#define ID_LBL_SYSVER     1101
#define ID_VAL_SYSVER     1102
#define ID_LBL_SUPP       1103
#define ID_VAL_SUPP       1104
#define ID_LBL_STATE      1105
#define ID_VAL_STATE      1106
#define ID_LBL_AUTO       1107
#define ID_VAL_AUTO       1108
#define ID_LBL_SVC        1109
#define ID_VAL_SVC        1110
#define ID_STATUS         1111   // bottom status line
#define ID_GITHUB         1112   // syslink

#define WM_WORKER_DONE (WM_USER + 1)
#define WM_UPDATE_STATUS (WM_USER + 2)
#define TIMER_STATUS 1
#define TIMER_FIRST_STATUS 2

static HWND g_hWnd = NULL;
static HWND g_hStatus = NULL;       // bottom status line (replaces log window)
static HANDLE g_hWorker = NULL;
static volatile LONG g_bBusy = 0;
static HFONT g_hFont = NULL;
static bool g_autoSupport = false;
static bool g_bootAuto = false;
static bool g_silent = false;
static bool g_statusLoggedOnce = false;
static HWND g_val[5] = {0};         // sysver / supp / state / auto / svc values
static wchar_t g_szLogFile[MAX_PATH] = {0};

// ---------------- utility ----------------
static std::wstring ExeDir()
{
    wchar_t p[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, p, MAX_PATH);
    PathRemoveFileSpecW(p);
    return p;
}

static void InitLogFile()
{
    // 日志统一写「系统默认 ANSI（中文 Windows = GBK）、不带 BOM」。
    // 原因：上一代用 UTF-8+BOM，结果 Notepad/cmd 默认按 GBK 解码，BOM(EF BB BF) 被显示成
    // “锘縖”、正文全乱码。写 GBK 无 BOM 后，Notepad / cmd / 各类中文工具都能直接读。
    // 迁移兼容：若旧日志带任意 UTF BOM（UTF-8 / UTF-16 LE / UTF-16 BE），说明是老编码，
    // 直接清零，避免新旧两种编码混编在同一文件里。
    if (!g_szLogFile[0]) return;
    HANDLE f = CreateFileW(g_szLogFile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    bool needReset = false;
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    if (sz.QuadPart >= 3) {
        BYTE head[3] = {0}; DWORD rd = 0;
        ReadFile(f, head, 3, &rd, NULL);
        if (rd == 3 &&
            ((head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) ||  // UTF-8 BOM
             (head[0] == 0xFF && head[1] == 0xFE) ||                      // UTF-16 LE BOM（第三字节是正文，不可参与判断）
             (head[0] == 0xFE && head[1] == 0xFF)))                       // UTF-16 BE BOM
            needReset = true;
    } else if (sz.QuadPart == 2) {
        BYTE head[2] = {0}; DWORD rd = 0;
        ReadFile(f, head, 2, &rd, NULL);
        if (rd == 2 &&
            ((head[0] == 0xFF && head[1] == 0xFE) ||  // UTF-16 LE BOM
             (head[0] == 0xFE && head[1] == 0xFF)))    // UTF-16 BE BOM
            needReset = true;
    }
    if (needReset) {
        SetFilePointer(f, 0, NULL, FILE_BEGIN);
        SetEndOfFile(f);   // 老编码日志清零，后续追加的都将为干净 GBK
    }
    CloseHandle(f);
}

// 把无法在系统 ANSI(GBK) 表达的字符替换成 ASCII 等价物，避免落盘成 '?' 乱码。
// 已知会进日志的特殊字符：→ (U+2192) 等；✓/✗ 仅在按钮标题(SetWindowTextW)显示，不落盘，但一并兜底。
static std::wstring SanitizeForAcp(const wchar_t* in)
{
    std::wstring out;
    for (const wchar_t* p = in; *p; ++p) {
        switch (*p) {
            case L'→': out += L"->"; break;   // 右箭头 →  不可在 GBK 表达
            case L'←': out += L"<-"; break;
            case L'↔': out += L"<->"; break;
            case L'✓': out += L"[OK]"; break; // 对勾
            case L'✗': out += L"[X]"; break;
            default:  out += *p; break;
        }
    }
    return out;
}

static void LogToFile(const wchar_t* line)
{
    if (!g_szLogFile[0]) return;
    // 先清洗不可表达字符，再用系统默认 ANSI（GBK）编码落盘，不带 BOM。
    std::wstring w = SanitizeForAcp(line);
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    if (n <= 1) return;
    std::vector<char> mb(n);
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, mb.data(), n, NULL, NULL);
    HANDLE f = CreateFileW(g_szLogFile, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wr;
    WriteFile(f, mb.data(), (DWORD)(n - 1), &wr, NULL);
    CloseHandle(f);
}

static void Log(const wchar_t* fmt, ...)
{
    wchar_t buf[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 4096, fmt, ap);
    va_end(ap);
    buf[4095] = 0;   // MSVC 语义截断时不补 NUL，手动终止防 wcslen 越界

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[4300];
    _snwprintf(line, 4300, L"[%02d:%02d:%02d] %s\r\n", st.wHour, st.wMinute, st.wSecond, buf);
    line[4299] = 0;

    LogToFile(line);
    OutputDebugStringW(line);
    // mirror the latest message onto the bottom status line (1:1: no log window)
    if (g_hStatus) {
        // trim to ~90 chars so it fits the 446px line
        size_t L = wcslen(buf);
        if (L > 90) { wmemmove(buf, buf + L - 90, 90); buf[90] = 0; }
        SetWindowTextW(g_hStatus, buf);
    }
}

static void SetBusy(bool busy)
{
    InterlockedExchange(&g_bBusy, busy ? 1 : 0);
    static const int ids[] = {ID_BTN_INSTALL, ID_BTN_UNINSTALL, ID_BTN_STARTSVC,
                              ID_BTN_STOPSVc, ID_BTN_SYNC, ID_BTN_UPDATE,
                              ID_BTN_AUTOA, ID_BTN_AUTOB};
    for (int id : ids)
    {
        HWND h = GetDlgItem(g_hWnd, id);
        if (h) EnableWindow(h, !busy);
    }
}

// ---------------- termsrv analyzer (AutoSupport) ----------------
struct AnalyzeResult {
    bool  ok;
    bool  defPolicyFound, singleUserFound, localOnlyFound, slInitFound;
    DWORD defPolicy, singleUser, localOnly, slInit;
    wchar_t ver[64];
};

// byte pattern with wildcards; derived & validated against termsrv.dll 10.0.28000.1761
struct Pattern { const char* hex; };  // "48 83 EC 28 8B 81 ?? ?? ?? ?? ..." - ?? = wildcard

static bool MatchAt(const BYTE* d, SIZE_T i, const BYTE* pat, const char* mask, size_t len)
{
    for (size_t j = 0; j < len; j++)
        if (mask[j] == 'x' && d[i + j] != pat[j]) return false;
    return true;
}

static bool CompilePattern(const char* hex, std::vector<BYTE>& pat, std::string& mask)
{
    pat.clear(); mask.clear();
    char tmp[8]; int k = 0;
    for (const char* p = hex; ; p++) {
        if (*p && *p != ' ') { tmp[k++] = *p; if (k > 2) return false; }
        else if (k) {
            tmp[k] = 0; k = 0;
            if (!strcmp(tmp, "??")) { pat.push_back(0); mask += '?'; }
            else { pat.push_back((BYTE)strtoul(tmp, NULL, 16)); mask += 'x'; }
        }
        if (!*p) break;
    }
    return pat.size() > 0;
}

// returns first hit offset or -1; restricts search to section-ish range [begin, end)
static long long ScanFirst(const BYTE* d, SIZE_T len, SIZE_T begin, SIZE_T end,
                           const char* hexPattern, bool (*extra)(const BYTE*, SIZE_T) = NULL)
{
    std::vector<BYTE> pat; std::string mask;
    if (!CompilePattern(hexPattern, pat, mask)) return -1;
    if (end > len) end = len;
    for (SIZE_T i = begin; i + pat.size() <= end; i++) {
        if (MatchAt(d, i, pat.data(), mask.c_str(), pat.size())) {
            if (!extra || extra(d, i)) return (long long)i;
        }
    }
    return -1;
}

// CDefPolicy::Query prologue (x64, Win10/11 style):
//   48 83 EC 28 | 8B 81 ?? ?? ?? ?? | 45 33 C0 | 89 02 | 8B 81 38 06 00 00 | 39 81 3C 06 00 00 | 75 16
// patch point = prologue + 15 (the second "mov eax,[rcx+638h]")
static const char* PAT_DEFPOLICY =
    "48 83 EC 28 8B 81 ?? ?? ?? ?? 45 33 C0 89 02 8B 81 38 06 00 00 39 81 3C 06 00 00 75 16";
// relaxed fallback: member offsets wildcarded (still requires the cmp/jne shape)
static const char* PAT_DEFPOLICY_LOOSE =
    "48 83 EC 28 8B 81 ?? ?? ?? ?? 45 33 C0 89 02 8B 81 ?? ?? 00 00 39 81 ?? ?? 00 00 75 16";

// SingleUser: C7 06 01 00 00 00 (mov dword [rsi],1) ; patch imm32 first byte -> Zero
// offset = hit + 2
static const char* PAT_SINGLEUSER =
    "C7 06 01 00 00 00 41 B0 07 66 89 85";

// LocalOnly: 83 3D ?? ?? ?? ?? 02 (cmp dword [rip+X],2) BB 05 00 07 80 0F 86
// patch point = the "74 xx" (jz) 2 bytes before hit
static bool ExtraLocalOnly(const BYTE* d, SIZE_T i) {
    return i >= 2 && d[i - 2] == 0x74 && d[i - 1] < 0x80;  // jz short
}
static const char* PAT_LOCALONLY =
    "83 3D ?? ?? ?? ?? 02 BB 05 00 07 80 0F 86";

// CSLQuery_Initialize entry: 40 57 48 81 EC ?? ?? 00 00 (push rdi; sub rsp,X)
//   preceded by alignment padding (CC)
static bool ExtraSLInit(const BYTE* d, SIZE_T i) {
    return i >= 1 && d[i - 1] == 0xCC;
}
static const char* PAT_SLINIT =
    "40 57 48 81 EC ?? ?? 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4";

static bool GetTermsrvVersion(const wchar_t* dllPath, wchar_t* ver, size_t cch)
{
    DWORD h = 0;
    DWORD sz = GetFileVersionInfoSizeW(dllPath, &h);
    if (!sz) return false;
    std::vector<BYTE> data(sz);
    if (!GetFileVersionInfoW(dllPath, 0, sz, data.data())) return false;
    struct LANGCP { WORD lang, cp; } *lc = NULL;
    UINT n = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&lc, &n) || n < 1) return false;
    wchar_t sub[64];
    _snwprintf(sub, 64, L"\\StringFileInfo\\%04X%04X\\FileVersion", lc[0].lang, lc[0].cp);
    wchar_t* s = NULL;
    if (!VerQueryValueW(data.data(), sub, (LPVOID*)&s, &n) || !s) return false;
    wcsncpy(ver, s, cch - 1); ver[cch - 1] = 0;
    // cut trailing " (WinBuild...)" noise if any
    wchar_t* sp = wcschr(ver, L' ');
    if (sp) *sp = 0;
    return wcslen(ver) > 0;
}

// reads file into buffer
static bool ReadWholeFile(const wchar_t* path, std::vector<BYTE>& out)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li; GetFileSizeEx(f, &li);
    out.resize((SIZE_T)li.QuadPart);
    DWORD rd = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(f, out.data(), (DWORD)out.size(), &rd, NULL);
    CloseHandle(f);
    return ok && rd == out.size();
}

static bool AnalyzeTermsrv(AnalyzeResult& r)
{
    memset(&r, 0, sizeof(r));
    wchar_t sys[MAX_PATH] = {0}, dll[MAX_PATH] = {0};
    GetSystemDirectoryW(sys, MAX_PATH);
    wcscpy(dll, sys); PathAppendW(dll, L"termsrv.dll");

    if (!GetTermsrvVersion(dll, r.ver, 64)) return false;

    std::vector<BYTE> d;
    if (!ReadWholeFile(dll, d)) return false;

    // arch check: must be PE32+ x64 (x86 GUI cannot patch x64 termsrv)
    if (d.size() < 0x200 || memcmp(d.data(), "MZ", 2)) return false;
    DWORD peOff = *(DWORD*)(d.data() + 0x3c);
    // #16: 不可信 PE 偏移；下界 + 64 位防回绕（peOff 为垃圾大值时 peOff+6 在 32 位下会回绕通过判断）
    if (peOff < 0x40 || (SIZE_T)peOff + 6 > d.size()) return false;
    WORD machine = *(WORD*)(d.data() + peOff + 4);
    if (machine != 0x8664) {
        Log(L"[-] AutoSupport: termsrv.dll 不是 x64 (machine=0x%04X)，请使用对应架构版本", machine);
        return false;
    }

    // scan executable-ish region (skip headers)
    long long v;
    v = ScanFirst(d.data(), d.size(), 0x400, d.size(), PAT_DEFPOLICY);
    if (v < 0) v = ScanFirst(d.data(), d.size(), 0x400, d.size(), PAT_DEFPOLICY_LOOSE);
    if (v >= 0) { r.defPolicy = (DWORD)(v + 15); r.defPolicyFound = true; }

    v = ScanFirst(d.data(), d.size(), 0x400, d.size(), PAT_SINGLEUSER);
    if (v >= 0) { r.singleUser = (DWORD)(v + 2); r.singleUserFound = true; }

    v = ScanFirst(d.data(), d.size(), 0x400, d.size(), PAT_LOCALONLY, ExtraLocalOnly);
    if (v >= 0) { r.localOnly = (DWORD)(v - 2); r.localOnlyFound = true; }

    v = ScanFirst(d.data(), d.size(), 0x400, d.size(), PAT_SLINIT, ExtraSLInit);
    if (v >= 0) { r.slInit = (DWORD)v; r.slInitFound = true; }

    // DefPolicy is the critical one for concurrent sessions
    r.ok = r.defPolicyFound;
    return r.ok;
}

// ---------------- ini helpers ----------------
static std::wstring IniPath()
{
    std::wstring p = ExeDir();
    p += L"\\rdpwrap.ini";
    return p;
}

static bool IniHasSection(const wchar_t* ini, const wchar_t* ver)
{
    wchar_t want[128]; _snwprintf(want, 128, L"[%s]", ver); want[127] = 0;
    HANDLE f = CreateFileW(ini, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::string line;
    bool found = false;
    // 单行比对（trim 后 UTF-8 转宽字符，忽略大小写）
    auto checkLine = [&](std::string& ln) {
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
        int n = MultiByteToWideChar(CP_UTF8, 0, ln.c_str(), -1, NULL, 0);
        if (n > 0) {
            std::vector<wchar_t> w(n);
            MultiByteToWideChar(CP_UTF8, 0, ln.c_str(), -1, w.data(), n);
            if (!_wcsicmp(w.data(), want)) return true;
        }
        return false;
    };
    // 块读取（64KB），避免逐字节 ReadFile 造成 55 万次系统调用
    char buf[65536]; DWORD rd;
    while (ReadFile(f, buf, sizeof(buf), &rd, NULL) && rd) {
        for (DWORD i = 0; i < rd; i++) {
            char c = buf[i];
            if (c == '\n') {
                if (checkLine(line)) { found = true; break; }
                line.clear();
            } else line += c;
        }
        if (found) break;
    }
    // 文件末尾无换行时，最后一行尚未参与比对
    if (!found && !line.empty()) found = checkLine(line);
    CloseHandle(f);
    return found;
}

static bool AppendIniSection(const wchar_t* ini, const wchar_t* ver, const AnalyzeResult& r)
{
    HANDLE f = CreateFileW(ini, FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    char sec[2044]; int n = 0; sec[0] = 0;
    // #12: 累加时防护剩余长度；截断时 k 钳位到实际写入长度，n 始终等于真实内容长，sec 始终有终止符
#define APP_SEC(...) do { \
        if (n >= 0 && n < 2043) { \
            int rem = 2043 - n; \
            int k = _snprintf(sec + n, rem + 1, __VA_ARGS__); \
            if (k < 0 || k > rem) k = rem; \
            n += k; sec[n] = '\0'; \
        } \
    } while (0)
    APP_SEC("\r\n[%ls]\r\n", ver);
    if (r.localOnlyFound)
        APP_SEC("LocalOnlyPatch.x64=1\r\nLocalOnlyOffset.x64=%X\r\nLocalOnlyCode.x64=jmpshort\r\n", r.localOnly);
    if (r.singleUserFound)
        APP_SEC("SingleUserPatch.x64=1\r\nSingleUserOffset.x64=%X\r\nSingleUserCode.x64=Zero\r\n", r.singleUser);
    if (r.defPolicyFound)
        APP_SEC("DefPolicyPatch.x64=1\r\nDefPolicyOffset.x64=%X\r\nDefPolicyCode.x64=CDefPolicy_Query_eax_rcx_jmp\r\n", r.defPolicy);
    if (r.slInitFound)
        APP_SEC("SLInitHook.x64=1\r\nSLInitOffset.x64=%X\r\nSLInitFunc.x64=New_CSLQuery_Initialize\r\n", r.slInit);
#undef APP_SEC
    DWORD wr = 0;
    BOOL ok = WriteFile(f, sec, (DWORD)n, &wr, NULL);
    CloseHandle(f);
    return ok && wr == (DWORD)n;
}

// ---------------- download ----------------
static const wchar_t* kIniUrls[] = {
    L"https://raw.githubusercontent.com/sebaxakerhtc/rdpwrap.ini/master/rdpwrap.ini",
    L"https://raw.githubusercontent.com/asmtron/rdpwrap/master/res/rdpwrap.ini",
    L"https://raw.githubusercontent.com/stascorp/rdpwrap/master/res/rdpwrap.ini",
};

// changed（可选）：输出"内容是否真的变化"。LastWriteTime 不可靠——
// URLDownloadToFileW 的时间戳取决于代理是否透传 Last-Modified，
// 代理剥离该头时会误判为变化，导致开机静默模式每次启动都卸载+重装
static bool DownloadIni(const wchar_t* dest, bool verbose, bool* changed = NULL)
{
    if (changed) *changed = true;  // 保守默认：无法确认时按已变化处理
    wchar_t tmp[MAX_PATH]; _snwprintf(tmp, MAX_PATH, L"%s.new", dest); tmp[MAX_PATH - 1] = 0;
    // 国内直连 raw.githubusercontent.com 基本失败，统一走 GitHub 代理；代理失败再回退直连
    const wchar_t* kProxyPrefix = L"https://gh-proxy.com/";
    for (auto url : kIniUrls) {
        // 1) 先尝试代理
        wchar_t proxyUrl[512];
        _snwprintf(proxyUrl, 512, L"%ls%ls", kProxyPrefix, url);
        DeleteFileW(tmp);
        if (verbose) Log(L"[*] downloading via proxy: %ls", proxyUrl);
        HRESULT hr = URLDownloadToFileW(NULL, proxyUrl, tmp, 0, NULL);
        if (FAILED(hr)) {
            // 2) 代理失败，回退直连
            if (verbose) Log(L"[-] proxy failed (hr=0x%08X), trying direct: %ls", hr, url);
            DeleteFileW(tmp);
            if (verbose) Log(L"[*] downloading: %ls", url);
            hr = URLDownloadToFileW(NULL, url, tmp, 0, NULL);
        }
        if (FAILED(hr)) { if (verbose) Log(L"[-] download failed (hr=0x%08X), trying next source", hr); continue; }
        // validate: size + section markers（扫描全文件；[PatchCodes] 位于文件末尾，前 8192 字节找不到会永远失败）
        std::vector<BYTE> buf;
        if (!ReadWholeFile(tmp, buf) || buf.size() < 100 * 1024) {
            if (verbose) Log(L"[-] downloaded file failed validation (size/read), trying next source");
            DeleteFileW(tmp);
            continue;
        }
        buf.push_back(0);  // NUL 终止，便于 strstr
        const char* pData = reinterpret_cast<const char*>(buf.data());
        if (!strstr(pData, "[PatchCodes]") || !strstr(pData, "[Main]")) {
            if (verbose) Log(L"[-] downloaded file failed section validation, trying next source");
            DeleteFileW(tmp);
            continue;
        }
        // backup old（只拷贝不移动：替换失败时原文件仍在原位），再替换
        wchar_t bak[MAX_PATH]; _snwprintf(bak, MAX_PATH, L"%s.bak", dest); bak[MAX_PATH - 1] = 0;
        DeleteFileW(bak);               // 先删旧备份，否则第二次起 CopyFileW 静默失败、备份永远陈旧
        CopyFileW(dest, bak, FALSE);    // dest 不存在时失败无所谓
        // 内容级比较：避免时间戳误判（见函数头注释）
        std::vector<BYTE> oldBuf;
        if (ReadWholeFile(dest, oldBuf) && oldBuf.size() + 1 == buf.size() &&
            memcmp(oldBuf.data(), buf.data(), oldBuf.size()) == 0) {
            if (changed) *changed = false;
            DeleteFileW(tmp);           // 内容未变，无需替换
            if (verbose) Log(L"[*] config unchanged, skip replace");
            return true;
        }
        if (!MoveFileW(tmp, dest)) { if (verbose) Log(L"[-] replace config failed: %u", GetLastError()); return false; }
        if (verbose) Log(L"[+] config synced (%.0f KB)", (double)buf.size() / 1024.0);
        return true;
    }
    return false;
}

// ---------------- console child runner ----------------
static std::wstring FindConsoleExe()
{
    static const wchar_t* names[] = { L"SuperRDP.exe" };
    for (auto n : names) {
        std::wstring p = ExeDir(); p += L"\\"; p += n;
        if (PathFileExistsW(p.c_str())) return p;
    }
    return L"";
}

// ---------------- 子进程输出解码 ----------------
// 控制台子进程（SuperRDP.exe 及其 system() 派生的 netsh）输出采用 OEM 码页
// （中文 Windows = GBK）。此前按块「先猜 UTF-8 再退回 ACP」的启发式解码，
// 一旦 GBK 双字节汉字被管道读边界劈开，就会产生「已删？？ 1 规则？？」
// 之类的乱码。现固定按 OEM 码页解码，并把块尾未闭合的引导字节留到
// 下一块拼接，避免多字节字符被截断。
static UINT  ChildOutputCP() { UINT cp = GetOEMCP(); return cp ? cp : GetACP(); }
static int   g_mbLeftLen = 0;              // 上一块遗留的未闭合引导字节数（0..1）
static char  g_mbLeft[4];

static void ResetChildDecoder()
{
    g_mbLeftLen = 0;
}

static void AppendToLogMB(const char* s, DWORD len)
{
    if (!len && !g_mbLeftLen) return;
    // 拼接上一块遗留的半截多字节字符
    char stack[4100];
    std::vector<char> tmp;
    char* p = stack;
    DWORD total = (DWORD)g_mbLeftLen + len;
    if (total > sizeof(stack)) {
        tmp.resize(total);
        p = tmp.data();
    }
    if (g_mbLeftLen) memcpy(p, g_mbLeft, g_mbLeftLen);
    if (len) memcpy(p + g_mbLeftLen, s, len);
    g_mbLeftLen = 0;

    // 块尾若是 DBCS 引导字节，说明字符被读边界劈开：保留到下一块再解码
    // （仅当本次确有新数据时才缓存；len==0 表示收尾 flush，直接解码残留字节）
    UINT cp = ChildOutputCP();
    CPINFO ci;
    DWORD cont = total;
    if (cont > 0 && len > 0 && GetCPInfo(cp, &ci) && ci.MaxCharSize > 1 && ci.LeadByte[0] != 0) {
        if (IsDBCSLeadByteEx(cp, (BYTE)p[cont - 1])) {
            g_mbLeft[0] = p[cont - 1];
            g_mbLeftLen = 1;
            cont--;
        }
    }
    if (!cont) return;

    int n = MultiByteToWideChar(cp, 0, p, (int)cont, NULL, 0);
    if (n <= 0) return;
    std::vector<wchar_t> w(n + 3);
    MultiByteToWideChar(cp, 0, p, (int)cont, w.data(), n);
    int end = n;
    if (n >= 1 && w[n - 1] == L'\n') { w[n - 1] = L'\r'; w[n] = L'\n'; end = n + 1; }
    else { w[n] = L'\r'; w[n + 1] = L'\n'; end = n + 2; }
    w[end] = 0;
    LogToFile(w.data());
    // also mirror the last line into the bottom status (strip trailing CR/LF)
    if (g_hStatus) {
        int last = end - 1;
        while (last >= 0 && (w[last] == L'\r' || w[last] == L'\n')) last--;
        int first = last;
        while (first >= 0 && w[first] != L'\n' && w[first] != L'\r') first--;
        if (last > first) {
            int L = last - first;
            if (L > 90) first = last - 90;
            w[last + 1] = 0;
            SetWindowTextW(g_hStatus, w.data() + first + 1);
        }
    }
}

struct ChildCtx { HANDLE inW; };

// runs console installer, feeds option ("1"/"2"/"3"), streams output into log.
// returns child exit code; -1 on create failure
static int RunInstaller(const wchar_t* option, const wchar_t* extraArg = NULL)
{
    std::wstring exe = FindConsoleExe();
    if (exe.empty()) {
        Log(L"[-] 找不到 SuperRDP.exe，请将 GUI 与安装器放在同一目录");
        return -1;
    }
    // sanity: dll + ini must sit next to console exe
    std::wstring base = exe.substr(0, exe.rfind(L'\\'));
    if (!PathFileExistsW((base + L"\\rdpwrap.dll").c_str()) ||
        !PathFileExistsW((base + L"\\rdpwrap.ini").c_str())) {
        Log(L"[-] 同目录缺少 rdpwrap.dll / rdpwrap.ini");
        return -1;
    }

    wchar_t cmd[MAX_PATH + 64];
    if (extraArg)
        _snwprintf(cmd, MAX_PATH + 64, L"\"%ls\" %ls", exe.c_str(), extraArg);
    else
        _snwprintf(cmd, MAX_PATH + 64, L"\"%ls\"", exe.c_str());
    cmd[MAX_PATH + 63] = 0;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE outR = NULL, outW = NULL, inR = NULL, inW = NULL;
    CreatePipe(&outR, &outW, &sa, 0);
    CreatePipe(&inR, &inW, &sa, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { 0 }; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outW; si.hStdError = outW; si.hStdInput = inR;
    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        Log(L"[-] CreateProcess failed: %u", GetLastError());
        CloseHandle(outR); CloseHandle(outW); CloseHandle(inR); CloseHandle(inW);
        return -1;
    }
    CloseHandle(outW); CloseHandle(inR);

    if (option && *option) {
        // option 是宽字符串，管道端（控制台安装器）按 ACP 读取输入；
        // 使用 WideCharToMultiByte + 换行，避免 char 小缓冲区溢出和乱码
        int wlen = (int)wcslen(option);
        int mbLen = WideCharToMultiByte(CP_ACP, 0, option, wlen, NULL, 0, NULL, NULL);
        if (mbLen > 0) {
            const int kExtraLF = 1; // '\n'
            char* pBuf = new char[mbLen + kExtraLF];
            if (pBuf) {
                WideCharToMultiByte(CP_ACP, 0, option, wlen, pBuf, mbLen, NULL, NULL);
                pBuf[mbLen] = '\n';
                DWORD wr;
                WriteFile(inW, pBuf, mbLen + kExtraLF, &wr, NULL);
                delete[] pBuf;
            }
        }
    }

    // poll pipe with timeout so we can feed "press any key" pauses and detect exit
    ResetChildDecoder();   // 每次运行重新初始化输出解码状态（清掉跨块残留字节）
    char rbuf[4096];
    DWORD rd = 0, wr = 0;
    int quietMs = 0, nudges = 0;
    while (nudges < 6) {
        DWORD avail = 0;
        if (!PeekNamedPipe(outR, NULL, 0, NULL, &avail, NULL)) break;  // pipe closed
        if (avail > 0) {
            DWORD chunk = avail < sizeof(rbuf) ? avail : (DWORD)sizeof(rbuf);
            if (!ReadFile(outR, rbuf, chunk, &rd, NULL) || rd == 0) break;
            AppendToLogMB(rbuf, rd);
            quietMs = 0;
        } else {
            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;
            quietMs += 100;
            // installer idle 1.5s -> feed newline (scanf leftovers / pause prompt)
            if (quietMs >= 1500) {
                quietMs = 0;
                WriteFile(inW, "\n", 1, &wr, NULL);
                FlushFileBuffers(inW);
                nudges++;
            }
            Sleep(100);
        }
    }
    // 先确保子进程退出，再排空残余输出。
    // 若先阻塞 ReadFile 排空：管道写端被子进程/孙进程占用且不输出时会永久阻塞，
    // 超时/强杀永远执行不到 → worker 挂死 → 所有按钮永久禁用
    if (WaitForSingleObject(pi.hProcess, 5000) == WAIT_TIMEOUT) {
        Log(L"[!] 安装器未正常退出，强制结束");
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 3000);
    }
    // 非阻塞排空（Peek + 截止时间）：进程已退出，写端关闭后自然 EOF
    DWORD drainStart = GetTickCount();
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(outR, NULL, 0, NULL, &avail, NULL)) break;  // pipe closed
        if (avail > 0) {
            if (!ReadFile(outR, rbuf, sizeof(rbuf), &rd, NULL) || rd == 0) break;
            AppendToLogMB(rbuf, rd);
            continue;
        }
        if (GetTickCount() - drainStart >= 1000) break;
        Sleep(50);
    }
    AppendToLogMB(NULL, 0);   // 冲刷解码器中可能残留的半截多字节字符
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    CloseHandle(outR); CloseHandle(inW);
    return (int)code;
}

// try to clone a sibling -SLInit data section (same a.b.c series) under a new name.
// data offsets are typically stable within one build series (e.g. 10.0.28000.*)
// 多个候选时选第 4 段 build 号与目标最接近的（README：克隆自同系列最近版本）
static bool CopySiblingSLInit(const wchar_t* ini, const wchar_t* ver, std::string& out)
{
    // ver = "a.b.c.d" -> prefix "a.b.c."
    wchar_t wprefix[64];
    wcsncpy(wprefix, ver, 63); wprefix[63] = 0;
    wchar_t* last = wcsrchr(wprefix, L'.');
    if (!last) return false;
    *(last + 1) = 0;  // "10.0.28000."
    size_t prefixLen = wcslen(wprefix);
    // 目标的第 4 段 build 号
    const wchar_t* dver = wcsrchr(ver, L'.');
    long long targetBuild = dver ? _wcstoi64(dver + 1, NULL, 10) : -1;

    HANDLE f = CreateFileW(ini, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    struct Cand { long long dist; std::vector<std::string> body; };
    std::vector<Cand> cands;
    std::vector<std::string> body;
    long long curDist = 0;
    bool copying = false;
    std::string line;
    // 块读取（64KB），避免逐字节 ReadFile 造成 55 万次系统调用
    char buf[65536]; DWORD rd;
    while (ReadFile(f, buf, sizeof(buf), &rd, NULL) && rd) {
        for (DWORD i = 0; i < rd; i++) {
            char c = buf[i];
            if (c == '\n') {
                while (!line.empty() && (line.back() == '\r')) line.pop_back();
                if (line.size() > 1 && line[0] == '[') {
                    if (copying) {  // 上一个候选段结束，收入列表继续找下一个
                        Cand cd; cd.dist = curDist; cd.body.swap(body);
                        if (!cd.body.empty()) cands.push_back(cd);
                        copying = false;
                    }
                    // check "[a.b.c.*-SLInit]"
                    if (line.find("-SLInit]") != std::string::npos) {
                        int n = MultiByteToWideChar(CP_UTF8, 0, line.c_str() + 1, -1, NULL, 0);
                        if (n > 0) {
                            std::vector<wchar_t> w(n);
                            MultiByteToWideChar(CP_UTF8, 0, line.c_str() + 1, -1, w.data(), n);
                            if (wcsncmp(w.data(), wprefix, prefixLen) == 0) {
                                long long build = _wcstoi64(w.data() + prefixLen, NULL, 10);
                                curDist = (targetBuild < 0 || build < 0) ? LLONG_MAX
                                          : (build > targetBuild ? build - targetBuild : targetBuild - build);
                                body.clear();
                                copying = true;
                            }
                        }
                    }
                } else if (copying && !line.empty()) {
                    body.push_back(line);
                }
                line.clear();
            } else line += c;
        }
    }
    CloseHandle(f);
    if (copying) {  // 文件末尾的最后一个候选段
        Cand cd; cd.dist = curDist; cd.body.swap(body);
        if (!cd.body.empty()) cands.push_back(cd);
    }
    if (cands.empty()) return false;
    size_t bestIdx = 0;
    for (size_t i = 1; i < cands.size(); i++)
        if (cands[i].dist < cands[bestIdx].dist) bestIdx = i;
    char hdr[128];
    _snprintf(hdr, 128, "[%ls-SLInit]\r\n", ver); hdr[127] = 0;
    out = hdr;
    for (auto& l : cands[bestIdx].body) { out += l; out += "\r\n"; }
    return true;
}

// SuperRDP2 workflow: 备份当前配置到程序目录（写入前留底）
static void BackupIni(const wchar_t* ini)
{
    wchar_t bak[MAX_PATH]; _snwprintf(bak, MAX_PATH, L"%ls.autobak", ini); bak[MAX_PATH - 1] = 0;
    CopyFileW(ini, bak, FALSE);  // best-effort
}

// ---------------- AutoSupport wrapper ----------------
// returns: 0 = already supported / 1 = generated / -1 = failed
static int EnsureSupported(bool verbose)
{
    std::wstring ini = IniPath();
    if (!PathFileExistsW(ini.c_str())) {
        if (verbose) Log(L"[-] 未找到 rdpwrap.ini，请先同步最新配置");
        return -1;
    }
    AnalyzeResult r;
    if (!AnalyzeTermsrv(r)) {
        if (verbose) Log(L"[-] 自动分析失败：无法读取 termsrv.dll 或架构不符");
        return -1;
    }
    if (IniHasSection(ini.c_str(), r.ver)) {
        if (verbose) Log(L"[+] termsrv %ls 已被配置支持", r.ver);
        return 0;
    }
    if (verbose)
        Log(L"[*] termsrv %ls 不在配置中，自动分析补丁点：DefPolicy=%X SingleUser=%X LocalOnly=%X SLInit=%X",
            r.ver, r.defPolicy, r.singleUser, r.localOnly, r.slInitFound ? r.slInit : 0);
    // SuperRDP2 workflow: 备份当前配置到程序目录（写入前留底）
    BackupIni(ini.c_str());
    if (!AppendIniSection(ini.c_str(), r.ver, r)) {
        if (verbose) Log(L"[-] 写入配置失败");
        return -1;
    }
    if (verbose) Log(L"[+] 已自动生成 [%ls] 支持段并写入 rdpwrap.ini", r.ver);
    std::string slinit;
    if (CopySiblingSLInit(ini.c_str(), r.ver, slinit)) {
        HANDLE f2 = CreateFileW(ini.c_str(), FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f2 != INVALID_HANDLE_VALUE) {
            DWORD wr; WriteFile(f2, slinit.c_str(), (DWORD)slinit.size(), &wr, NULL);
            CloseHandle(f2);
            if (verbose) Log(L"[+] 已从同系列 build 克隆 [%ls-SLInit] 数据段", r.ver);
        }
    } else if (verbose) {
        Log(L"[!] 未找到同系列 -SLInit 数据段（策略数据将使用默认值）");
    }
    return 1;
}

// ---------------- boot autostart ----------------
static bool BootEnabled()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    wchar_t v[MAX_PATH]; DWORD sz = sizeof(v); DWORD t;
    bool on = (RegQueryValueExW(k, L"SuperRDP", 0, &t, (LPBYTE)v, &sz) == ERROR_SUCCESS);
    RegCloseKey(k);
    return on;
}

// 返回是否成功：失败时调用方需回滚 g_bootAuto，否则按钮 ✓ 状态与注册表不一致
static bool SetBoot(bool on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) {
        Log(L"[-] 无法写入启动项（需要管理员权限）");
        return false;
    }
    bool ok = true;
    if (on) {
        wchar_t p[MAX_PATH]; GetModuleFileNameW(NULL, p, MAX_PATH); p[MAX_PATH - 1] = 0;
        wchar_t v[MAX_PATH + 16]; _snwprintf(v, MAX_PATH + 16, L"\"%ls\" /silent", p); v[MAX_PATH + 15] = 0;
        if (RegSetValueExW(k, L"SuperRDP", 0, REG_SZ, (LPBYTE)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t))) != ERROR_SUCCESS) {
            Log(L"[-] 写入启动项失败（需要管理员权限）");
            ok = false;
        } else {
            Log(L"[+] 已开启开机自动支持（静默同步+分析+更新）");
        }
    } else {
        LSTATUS st = RegDeleteValueW(k, L"SuperRDP");
        // 值本来就不存在（ERROR_FILE_NOT_FOUND）也视为关闭成功
        if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) {
            Log(L"[-] 删除启动项失败：%u", (unsigned)st);
            ok = false;
        } else {
            Log(L"[+] 已关闭开机自动支持");
        }
    }
    RegCloseKey(k);
    return ok;
}

// ---------------- live status (replicates SuperRDP2 real-time check) ----------------
struct StatusInfo {
    bool installed, supported, svcRunning, listening;
    wchar_t termsrv[64];   // local termsrv.dll version
    wchar_t latest[64];    // newest build supported by the (online-synced) ini
};

// 解析单行 "[a.b.c.d]" / "[a.b.c.d-SLInit]"，命中且大于当前 best 则更新 out
static void ParseIniVersionLine(const char* line, int best[4], wchar_t* out, size_t cch)
{
    if (line[0] != '[') return;
    int v[4]; int n = 0; const char* p = line + 1;
    while (n < 4) {
        const char* e = p; long val = 0; int digits = 0;
        while (*e >= '0' && *e <= '9') { val = val * 10 + (*e - '0'); e++; digits++; }
        if (!digits) break;
        v[n++] = (int)val;
        if (*e != '.') break;
        p = e + 1;
    }
    if (n != 4) return;
    // section header must end right after the 4th number ("]" or "-SLInit]")
    const char* after = line + 1;
    for (int k = 0; k < 4; k++) { while (*after >= '0' && *after <= '9') after++; if (k < 3) after++; }
    if (*after != ']' && *after != '-') return;
    bool better = false;
    for (int k = 0; k < 4 && !better; k++) {
        if (v[k] > best[k]) better = true;
        if (v[k] < best[k]) break;
    }
    if (better) {
        memcpy(best, v, sizeof(v));
        _snwprintf(out, cch, L"%d.%d.%d.%d", v[0], v[1], v[2], v[3]);
        out[cch - 1] = 0;
    }
}

// scan ini for the highest "[a.b.c.d]" build section
static void LatestIniVersion(const wchar_t* ini, wchar_t* out, size_t cch)
{
    out[0] = 0;
    HANDLE f = CreateFileW(ini, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    int best[4] = {-1,-1,-1,-1};
    char line[512]; int li = 0; DWORD rd;
    // 块读取（64KB），避免逐字节 ReadFile 造成 55 万次系统调用
    char buf[65536];
    while (ReadFile(f, buf, sizeof(buf), &rd, NULL) && rd) {
        for (DWORD i = 0; i < rd; i++) {
            char c = buf[i];
            if (c == '\n') {
                line[li] = 0;
                ParseIniVersionLine(line, best, out, cch);
                li = 0;
            } else if (li < 511) line[li++] = c;
        }
    }
    // 文件末尾无换行时，最后一行也要参与解析
    if (li > 0) { line[li] = 0; ParseIniVersionLine(line, best, out, cch); }
    CloseHandle(f);
}

static bool IsInstalled()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\TermService\\Parameters", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    wchar_t v[MAX_PATH] = {0}; DWORD sz = sizeof(v); DWORD t;
    bool on = false;
    if (RegQueryValueExW(k, L"ServiceDll", 0, &t, (LPBYTE)v, &sz) == ERROR_SUCCESS) {
        wchar_t* name = wcsrchr(v, L'\\');
        on = (name && !_wcsicmp(name + 1, L"rdpwrap.dll"));
    }
    RegCloseKey(k);
    return on;
}

static void QueryStatus(StatusInfo& s)
{
    memset(&s, 0, sizeof(s));
    s.installed = IsInstalled();

    wchar_t sys[MAX_PATH], dll[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    wcscpy(dll, sys); PathAppendW(dll, L"termsrv.dll");
    if (GetTermsrvVersion(dll, s.termsrv, 64)) {
        std::wstring ini = IniPath();
        if (PathFileExistsW(ini.c_str())) {
            s.supported = IniHasSection(ini.c_str(), s.termsrv);
            LatestIniVersion(ini.c_str(), s.latest, 64);
        }
    }

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE svc = OpenServiceW(scm, L"TermService", SERVICE_QUERY_STATUS);
        if (svc) {
            SERVICE_STATUS_PROCESS st; DWORD need = 0;
            if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&st, sizeof(st), &need))
                s.svcRunning = (st.dwCurrentState == SERVICE_RUNNING);
            CloseServiceHandle(svc);
        }
        CloseServiceHandle(scm);
    }

    // 3389 listener probe (200ms timeout)
    SOCKET so = socket(AF_INET, SOCK_STREAM, 0);
    if (so != INVALID_SOCKET) {
        u_long nb = 1; ioctlsocket(so, FIONBIO, &nb);
        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET; sa.sin_port = htons(3389);
        sa.sin_addr.s_addr = htonl(0x7F000001);
        connect(so, (struct sockaddr*)&sa, sizeof(sa));
        fd_set w; FD_ZERO(&w); FD_SET(so, &w);
        struct timeval tv = {0, 200000};
        if (select(0, NULL, &w, NULL, &tv) > 0) {
            int err = 0; int len = sizeof(err);
            getsockopt(so, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            s.listening = (err == 0);
        }
        closesocket(so);
    }
}

// count active (connected) RDP sessions via WTS; returns 0 on failure
static int ActiveSessions()
{
    // 直接使用 WTS_CURRENT_SERVER_HANDLE 作为枚举句柄，无需 WTSOpenServerW
    PWTS_SESSION_INFO pInfo = NULL;
    DWORD count = 0;
    int active = 0;
    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pInfo, &count)) {
        for (DWORD i = 0; i < count; i++) {
            if (pInfo[i].State == WTSActive) active++;
        }
        WTSFreeMemory(pInfo);
    }
    return active;
}

// UI thread only
static void UpdateStatusUI(const StatusInfo& s)
{
    // 系统版本：/ termsrv：/ 状态：/ 自动分析：/ termsrv服务： rows
    SetWindowTextW(g_val[0], s.termsrv[0] ? s.termsrv : L"未知");
    SetWindowTextW(g_val[1], s.latest[0] ? s.latest : L"—");
    SetWindowTextW(g_val[2], s.installed ? L"已安装" : L"未安装");
    int sess = ActiveSessions();
    wchar_t conn[32]; _snwprintf(conn, 32, L"%d/9999", sess);
    SetWindowTextW(g_val[3], conn);
    wchar_t svc[64];
    _snwprintf(svc, 64, L"%s 3389: %s",
               s.svcRunning ? L"正在运行" : L"已停止",
               s.listening ? L"YES" : L"NO");
    SetWindowTextW(g_val[4], svc);

    // reflect toggle button captions
    HWND h;
    if ((h = GetDlgItem(g_hWnd, ID_BTN_AUTOA)))
        SetWindowTextW(h, g_autoSupport ? L"自动分析 ✓" : L"自动分析");
    if ((h = GetDlgItem(g_hWnd, ID_BTN_AUTOB)))
        SetWindowTextW(h, g_bootAuto ? L"开机启动 ✓" : L"开机启动");

    // bottom status line: emulate original "server is wrong." semantics
    if (g_hStatus) {
        if (!s.installed)
            SetWindowTextW(g_hStatus, L"server is wrong.");
        else if (!s.supported)
            SetWindowTextW(g_hStatus, L"已安装，但当前 termsrv 版本尚未被配置支持（请同步最新配置并开启自动分析）");
        else if (!s.svcRunning)
            SetWindowTextW(g_hStatus, L"已安装且已支持，但终端服务未运行（点击 启动）");
        else if (!s.listening)
            SetWindowTextW(g_hStatus, L"服务运行中，但 3389 端口未监听（尝试 启动/重启）");
        else
            SetWindowTextW(g_hStatus, L"一切正常：远程桌面已就绪 (RDP 3389)");
    }

    if (!g_statusLoggedOnce) {
        g_statusLoggedOnce = true;
        Log(L"当前状态：%ls / %ls / %ls / %ls",
            s.installed ? L"已安装" : L"未安装",
            s.supported ? L"配置已支持" : L"配置未支持",
            s.svcRunning ? L"服务运行中" : L"服务未运行",
            s.listening ? L"3389监听中" : L"3389未监听");
    }
}

static DWORD WINAPI StatusWorker(LPVOID)
{
    StatusInfo s;
    QueryStatus(s);
    // 投递失败（消息队列满/窗口已销毁）时释放对象，防泄漏
    StatusInfo* p = new StatusInfo(s);
    if (!PostMessage(g_hWnd, WM_UPDATE_STATUS, 0, (LPARAM)p)) delete p;
    return 0;
}

static void KickStatusRefresh()
{
    HANDLE t = CreateThread(NULL, 0, StatusWorker, NULL, 0, NULL);
    if (t) CloseHandle(t);   // CreateThread 失败时不关 NULL 句柄
}



static void DoStatus()
{
    Log(L"========= 状态检查 =========");
    wchar_t sys[MAX_PATH], dll[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    wcscpy(dll, sys); PathAppendW(dll, L"termsrv.dll");
    wchar_t ver[64] = L"?";
    GetTermsrvVersion(dll, ver, 64);
    Log(L"termsrv.dll: %ls  (%s)", ver, PathFileExistsW(dll) ? L"存在" : L"缺失");

    std::wstring ini = IniPath();
    if (PathFileExistsW(ini.c_str()))
        Log(L"rdpwrap.ini: %ls  本版本支持: %s", ini.c_str(),
            IniHasSection(ini.c_str(), ver) ? L"是" : L"否（可勾选自动分析后安装）");
    else
        Log(L"rdpwrap.ini: 未找到，请先点击 同步最新配置");

    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\TermService\\Parameters", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t v[MAX_PATH] = {0}; DWORD sz = sizeof(v); DWORD t;
        if (RegQueryValueExW(k, L"ServiceDll", 0, &t, (LPBYTE)v, &sz) == ERROR_SUCCESS)
            Log(L"TermService ServiceDll: %ls", v);
        RegCloseKey(k);
    }
    Log(L"开机自动支持: %s", BootEnabled() ? L"已开启" : L"未开启");
    Log(L"===========================");
}

// ---------------- service start / stop (启动 / 停止) ----------------
static bool ControlTermService(bool start)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) { Log(L"[-] 无法连接服务管理器: %u", GetLastError()); return false; }
    SC_HANDLE svc = OpenServiceW(scm, L"TermService", SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);
    if (!svc) { Log(L"[-] 打开 TermService 失败: %u", GetLastError()); CloseServiceHandle(scm); return false; }
    bool ok = false;
    if (start) {
        if (StartServiceW(svc, 0, NULL)) ok = true;
        else Log(L"[-] 启动 TermService 失败: %u", GetLastError());
    } else {
        SERVICE_STATUS st = {0};
        if (ControlService(svc, SERVICE_CONTROL_STOP, &st)) ok = true;
        else Log(L"[-] 停止 TermService 失败: %u", GetLastError());
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

// ---------------- actions (worker thread) ----------------
struct WorkerParam { int action; };

static DWORD WINAPI Worker(LPVOID p)
{
    int action = ((WorkerParam*)p)->action;
    delete (WorkerParam*)p;

    switch (action) {
    case ID_BTN_INSTALL:
        Log(L"========= 开始安装 =========");
        if (g_autoSupport) {
            Log(L"[*] AutoSupport：检查并自动支持当前系统…");
            EnsureSupported(true);
        }
        RunInstaller(L"1");
        Log(L"========= 安装流程结束 =========");
        break;
    case ID_BTN_UNINSTALL:
        Log(L"========= 开始卸载 =========");
        RunInstaller(L"2");
        Log(L"========= 卸载流程结束 =========");
        break;
    case ID_BTN_RESTART:
        Log(L"========= 强制重启终端服务 =========");
        RunInstaller(L"3");
        Log(L"========= 重启完成 =========");
        break;
    case ID_BTN_SYNC:
        Log(L"========= 同步最新配置 =========");
        if (DownloadIni((wchar_t*)IniPath().c_str(), true, NULL)) {
            if (g_autoSupport) EnsureSupported(true);
            // SuperRDP2 workflow: 拉取线上最新配置后自动重装
            // 注意必须走 update（卸载+重装）：菜单选项 1 在已安装时会被安装器早退，
            // 新 ini 不会拷入 System32、服务不重启，配置不生效
            if (IsInstalled()) {
                Log(L"[*] 检测到已安装，自动重新安装以应用新配置…");
                RunInstaller(L"\n", L"update");
            } else {
                Log(L"[*] 当前未安装，跳过自动重装（点击 安装 即可）");
            }
        } else {
            Log(L"[-] 所有配置源均不可用，请检查网络");
        }
        Log(L"===========================");
        break;
    case ID_BTN_UPDATE:
        Log(L"========= 更新 =========");
        if (DownloadIni((wchar_t*)IniPath().c_str(), true)) {
            if (g_autoSupport) EnsureSupported(true);
            Log(L"[*] 重新应用配置（卸载+重新安装）…");
            RunInstaller(L"\n", L"update");
        } else {
            Log(L"[-] 所有配置源均不可用，请检查网络");
        }
        Log(L"===========================");
        break;
    case ID_BTN_STARTSVC:
        Log(L"========= 启动终端服务 =========");
        if (ControlTermService(true)) Log(L"[+] TermService 已启动");
        else Log(L"[-] 启动失败（需要管理员权限）");
        Log(L"=============================");
        break;
    case ID_BTN_STOPSVc:
        Log(L"========= 停止终端服务 =========");
        if (ControlTermService(false)) Log(L"[+] TermService 已停止");
        else Log(L"[-] 停止失败（需要管理员权限）");
        Log(L"=============================");
        break;
    case ID_BTN_STATUS:
        DoStatus();
        break;
    }
    PostMessage(g_hWnd, WM_WORKER_DONE, 0, 0);
    return 0;
}

static void StartAction(int action)
{
    if (InterlockedCompareExchange(&g_bBusy, 1, 0)) return;
    // 前一个 worker 已退出（g_bBusy==0 才能到这里），关闭其句柄防止泄漏
    if (g_hWorker) { CloseHandle(g_hWorker); g_hWorker = NULL; }
    SetBusy(true);
    // g_autoSupport is owned by the 自动分析 toggle button (global), do not override here
    WorkerParam* p = new WorkerParam{ action };
    g_hWorker = CreateThread(NULL, 0, Worker, p, 0, NULL);
    if (!g_hWorker) {
        // CreateThread 失败：回滚状态，防止 GUI 永久禁用（死锁）
        InterlockedExchange(&g_bBusy, 0);
        SetBusy(false);
        delete p;
        Log(L"[!] CreateThread 失败，操作未执行");
    }
}

// ---------------- silent mode (boot) ----------------
static bool GetFileLastWrite(const std::wstring& path, FILETIME& ft)
{
    WIN32_FILE_ATTRIBUTE_DATA fi;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fi)) return false;
    ft = fi.ftLastWriteTime;
    return true;
}

static int SilentRun()
{
    // allocated console for child stdio is not needed; run headless
    // 1. sync, 2. autosupport, 3. config changed/new version & installed -> update
    std::wstring ini = IniPath();
    bool iniChanged = false;
    bool downloaded = DownloadIni((wchar_t*)ini.c_str(), false, &iniChanged);
    // 与 GUI 同步路径一致：ini 更新后也要重装才能把新配置带进 System32 并重启服务。
    // iniChanged 由 DownloadIni 内容级比较给出（不能用文件时间戳，代理可能剥离 Last-Modified）
    int st = EnsureSupported(false);
    // 必须带 downloaded 守卫：DownloadIni 入口保守设 changed=true，下载全部失败时
    // 若不判 downloaded 会在无网络时每次开机都触发无意义的卸载+重装（断开 RDP 会话）
    if (st == 1 || (downloaded && iniChanged && IsInstalled())) {
        // config extended / replaced -> refresh install quietly (console exe supports "update")
        // We must feed stdin for the trailing pause: use RunInstaller with extraArg.
        g_silent = true;
        RunInstaller(L"\n", L"update");
        g_silent = false;
    }
    return 0;
}

// ---------------- analyze-only test mode (verification) ----------------
static int AnalyzeTestMode()
{
    AnalyzeResult r;
    if (!AnalyzeTermsrv(r)) { printf("analyze failed\n"); return 1; }
    printf("version=%ls\n", r.ver);
    printf("DefPolicy=%X found=%d\n", r.defPolicy, r.defPolicyFound);
    printf("SingleUser=%X found=%d\n", r.singleUser, r.singleUserFound);
    printf("LocalOnly=%X found=%d\n", r.localOnly, r.localOnlyFound);
    printf("SLInit=%X found=%d\n", r.slInit, r.slInitFound);
    printf("ok=%d\n", r.ok);
    // exercise ini write path if a test rdpwrap.ini sits next to this exe
    std::wstring ini = ExeDir() + L"\\rdpwrap.ini";
    if (PathFileExistsW(ini.c_str())) {
        wchar_t latest[64] = {0};
        LatestIniVersion(ini.c_str(), latest, 64);
        printf("latest ini build: %ls\n", latest);
        printf("--- ini write path test ---\n");
        if (IniHasSection(ini.c_str(), r.ver)) { printf("already supported in test ini\n"); }
        else {
            BackupIni(ini.c_str());  // same as production path (verification): pre-write copy
            if (AppendIniSection(ini.c_str(), r.ver, r))
                printf("appended [%ls]\n", r.ver);
            else { printf("append FAILED\n"); return 0; }
            std::string sl;
            if (CopySiblingSLInit(ini.c_str(), r.ver, sl)) {
                HANDLE f = CreateFileW(ini.c_str(), FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (f != INVALID_HANDLE_VALUE) {
                    DWORD wr; WriteFile(f, sl.c_str(), (DWORD)sl.size(), &wr, NULL);
                    CloseHandle(f);
                    printf("cloned SLInit section (%zu bytes)\n", sl.size());
                } else printf("open ini for SLInit clone failed: %lu\n", GetLastError());
            } else printf("no sibling SLInit\n");
        }
    }
    return 0;
}

// ---------------- UI ----------------
static BOOL CALLBACK SetChildFont(HWND h, LPARAM)
{
    SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return TRUE;
}

// (no Layout: 1:1 replica uses fixed 471x240 client geometry)

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        // ---- big background container (overlaps the left info cluster) ----
        CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            4, 2, 336, 163, hwnd, (HMENU)ID_PANEL, NULL, NULL);

        // ---- left info rows (label : value) ----
        CreateWindowW(L"STATIC", L"系统版本：", WS_CHILD | WS_VISIBLE | SS_LEFT,
            7, 19, 86, 18, hwnd, (HMENU)ID_LBL_SYSVER, NULL, NULL);
        g_val[0] = CreateWindowW(L"STATIC", L"检查中…", WS_CHILD | WS_VISIBLE | SS_LEFT,
            86, 19, 112, 18, hwnd, (HMENU)ID_VAL_SYSVER, NULL, NULL);
        CreateWindowW(L"STATIC", L"termsrv：", WS_CHILD | WS_VISIBLE | SS_LEFT,
            7, 42, 86, 18, hwnd, (HMENU)ID_LBL_SUPP, NULL, NULL);
        g_val[1] = CreateWindowW(L"STATIC", L"—", WS_CHILD | WS_VISIBLE | SS_LEFT,
            86, 42, 119, 18, hwnd, (HMENU)ID_VAL_SUPP, NULL, NULL);
        CreateWindowW(L"STATIC", L"状态：", WS_CHILD | WS_VISIBLE | SS_LEFT,
            7, 70, 68, 18, hwnd, (HMENU)ID_LBL_STATE, NULL, NULL);
        g_val[2] = CreateWindowW(L"STATIC", L"未安装", WS_CHILD | WS_VISIBLE | SS_LEFT,
            86, 70, 98, 18, hwnd, (HMENU)ID_VAL_STATE, NULL, NULL);
        CreateWindowW(L"STATIC", L"自动分析：", WS_CHILD | WS_VISIBLE | SS_LEFT,
            7, 98, 86, 18, hwnd, (HMENU)ID_LBL_AUTO, NULL, NULL);
        g_val[3] = CreateWindowW(L"STATIC", L"0/9999", WS_CHILD | WS_VISIBLE | SS_LEFT,
            86, 98, 84, 18, hwnd, (HMENU)ID_VAL_AUTO, NULL, NULL);
        CreateWindowW(L"STATIC", L"termsrv服务：", WS_CHILD | WS_VISIBLE | SS_LEFT,
            7, 130, 79, 18, hwnd, (HMENU)ID_LBL_SVC, NULL, NULL);
        g_val[4] = CreateWindowW(L"STATIC", L"正在运行 3389: NO", WS_CHILD | WS_VISIBLE | SS_LEFT,
            86, 130, 130, 18, hwnd, (HMENU)ID_VAL_SVC, NULL, NULL);

        // ---- action buttons inside the panel ----
        CreateWindowW(L"BUTTON", L"同步最新配置", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            207, 35, 131, 25, hwnd, (HMENU)ID_BTN_SYNC, NULL, NULL);
        CreateWindowW(L"BUTTON", L"安装", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            207, 65, 63, 25, hwnd, (HMENU)ID_BTN_INSTALL, NULL, NULL);
        CreateWindowW(L"BUTTON", L"卸载", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            268, 65, 70, 25, hwnd, (HMENU)ID_BTN_UNINSTALL, NULL, NULL);
        // NOTE: 原版(及初版复刻)把 启动(242,102) 放在 github SysLink(205,98,105,25) 矩形内，
        // SysLink z-order 在上吃掉鼠标 → 启动点不动。若只下移 启动 到 y124，又会压到 termsrv 服务
        // 状态文本(86,130,180,18) → 同样被盖。修法：把 termsrv 文本控件宽度 180→130(结束于 x216)，
        // 让出右列，启动/停止 落在 x[220,338]×y[124,149]，同时避开 github 链接(y98-123)与 termsrv 文本(y130-148)。
        CreateWindowW(L"BUTTON", L"启动", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            220, 124, 58, 25, hwnd, (HMENU)ID_BTN_STARTSVC, NULL, NULL);
        CreateWindowW(L"BUTTON", L"停止", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            282, 124, 56, 25, hwnd, (HMENU)ID_BTN_STOPSVc, NULL, NULL);
        CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            175, 95, 26, 25, hwnd, (HMENU)ID_BTN_PLUS, NULL, NULL);
        CreateWindowW(L"SysLink", L"<a>share to github</a>", WS_CHILD | WS_VISIBLE,
            205, 98, 105, 25, hwnd, (HMENU)ID_GITHUB, NULL, NULL);

        // ---- right margin buttons (outside the panel) ----
        CreateWindowW(L"BUTTON", L"更新", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            341, 7, 109, 33, hwnd, (HMENU)ID_BTN_UPDATE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"自动分析", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            343, 88, 107, 18, hwnd, (HMENU)ID_BTN_AUTOA, NULL, NULL);
        CreateWindowW(L"BUTTON", L"开机启动", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            343, 110, 107, 18, hwnd, (HMENU)ID_BTN_AUTOB, NULL, NULL);

        // ---- bottom status line (1:1: "server is wrong." placeholder) ----
        g_hStatus = CreateWindowW(L"STATIC", L"server is wrong.", WS_CHILD | WS_VISIBLE | SS_LEFT,
            4, 170, 446, 18, hwnd, (HMENU)ID_STATUS, NULL, NULL);

        // fonts
        g_hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        EnumChildWindows(hwnd, SetChildFont, 0);

        // initial toggle states
        g_autoSupport = true;   // SuperRDP2 默认建议开启自动分析
        g_bootAuto = BootEnabled();

        // 立即同步 toggle 按钮初始文本，避免首次状态刷新（400ms 后）造成的视觉跳变
        SendDlgItemMessageW(hwnd, ID_BTN_AUTOA, WM_SETTEXT, 0,
            (LPARAM)(g_autoSupport ? L"自动分析 ✓" : L"自动分析"));
        SendDlgItemMessageW(hwnd, ID_BTN_AUTOB, WM_SETTEXT, 0,
            (LPARAM)(g_bootAuto ? L"开机启动 ✓" : L"开机启动"));

        // log file for diagnostics (GUI has no on-screen log window, 1:1)
        // 使用 _snwprintf 带长度限制，防止超长 ExeDir 路径导致 g_szLogFile 溢出
        _snwprintf(g_szLogFile, MAX_PATH, L"%ls\\SuperRDPGui.log", ExeDir().c_str());
        g_szLogFile[MAX_PATH - 1] = L'\0';
        InitLogFile();

        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) == 0) { /* keep ws2 alive for status probes */ }
        SetTimer(hwnd, TIMER_FIRST_STATUS, 400, NULL);   // first refresh shortly after show
        SetTimer(hwnd, TIMER_STATUS, 15000, NULL);       // real-time refresh every 15s
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_FIRST_STATUS) { KillTimer(hwnd, TIMER_FIRST_STATUS); KickStatusRefresh(); return 0; }
        if (wp == TIMER_STATUS)       { KickStatusRefresh(); return 0; }
        break;
    case WM_UPDATE_STATUS: {
        StatusInfo* s = (StatusInfo*)lp;
        if (s) { UpdateStatusUI(*s); delete s; }
        return 0;
    }
    case WM_WORKER_DONE:
        SetBusy(false);
        KickStatusRefresh();   // refresh after each action
        return 0;
    case WM_NOTIFY: {
        // SysLink "share to github" click -> open URL
        LPNMHDR hdr = (LPNMHDR)lp;
        if (hdr->idFrom == ID_GITHUB && hdr->code == NM_CLICK) {
            ShellExecuteW(NULL, L"open", L"https://github.com/sky12378/SuperRDP2-new", NULL, NULL, SW_SHOW);
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BTN_INSTALL:
        case ID_BTN_UNINSTALL:
        case ID_BTN_STARTSVC:
        case ID_BTN_STOPSVc:
        case ID_BTN_SYNC:
        case ID_BTN_UPDATE:
            if (HIWORD(wp) == BN_CLICKED) StartAction(LOWORD(wp));
            return 0;
        case ID_BTN_AUTOA:
            if (HIWORD(wp) == BN_CLICKED) {
                g_autoSupport = !g_autoSupport;
                if (g_autoSupport) Log(L"[*] 自动分析已开启");
                else Log(L"[*] 自动分析已关闭");
                KickStatusRefresh();
            }
            return 0;
        case ID_BTN_AUTOB:
            if (HIWORD(wp) == BN_CLICKED) {
                g_bootAuto = !g_bootAuto;
                // 写注册表失败（如非管理员）时回滚开关状态，避免按钮 ✓ 与实际不符
                if (!SetBoot(g_bootAuto)) g_bootAuto = !g_bootAuto;
                KickStatusRefresh();
            }
            return 0;
        case ID_BTN_PLUS:
            if (HIWORD(wp) == BN_CLICKED)
                StartAction(ID_BTN_RESTART);  // 强制重启服务（与 README 描述一致）
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmd, int show)
{
#ifdef SRDP_TEST
    (void)hInst; (void)hPrev; (void)cmd; (void)show;
    return AnalyzeTestMode();
#else
    // command line modes
    for (wchar_t* p = cmd; *p; ) {
        while (*p == L' ') p++;
        wchar_t* e = p; while (*e && *e != L' ') e++;
        size_t len = e - p;
        if (len == 7 && !_wcsnicmp(p, L"/silent", 7)) return SilentRun();
        if (len == 8 && !_wcsnicmp(p, L"/analyze", 8)) return AnalyzeTestMode();
        p = e;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SuperRDPGui";
    RegisterClassW(&wc);

    g_hWnd = CreateWindowW(wc.lpszClassName,
        L"SuperRDP2",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 471, 240, NULL, NULL, hInst, NULL);

    ShowWindow(g_hWnd, show ? show : SW_SHOW);
    UpdateWindow(g_hWnd);

    Log(L"SuperRDP GUI 已启动（管理员）");
    Log(L"建议流程：[同步最新配置] → 勾选[自动分析] → [安装 SuperRDP]");
    Log(L"若仍不支持，请到 https://github.com/sky12378/SuperRDP2-new/issues 提交 termsrv.dll");

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    // 统一在此释放 Winsock（WM_DESTROY → PostQuitMessage 后消息循环退出必经此处，
    // 避免在 WM_DESTROY 内重复调用 WSACleanup）
    WSACleanup();
    return (int)m.wParam;
#endif
}
