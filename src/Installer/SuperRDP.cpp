
#include "pch.h"
#include <stdio.h>
#include <iostream>
#include <windows.h>
#include <stddef.h>
#include <strsafe.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi")
//#include <sysinfoapi.h>
#include "Registry.h"
#include "resource.h"

#pragma comment(lib, "User32.lib")

#include "IniFile.h"
#include <new>

INI_FILE* IniFile = NULL;


//int SC_ENUM_PROCESS_INFO = 0;
wchar_t TermService[] = L"TermService";
bool Installed = false;
int Arch = 0;
PVOID OldWow64RedirectionValue = NULL;
std::wstring TermServicePath;
//FILE_VERSION FV = { 0 }; //
DWORD TermServicePID = 0;
wchar_t ShareSvc[100][MAX_PATH] = { 0 };
int ShareSvcCount = 0;

bool SupportedArchitecture()
{
    bool result = false;
    SYSTEM_INFO si ;
    GetNativeSystemInfo(&si);

    switch(si.wProcessorArchitecture ) {
    case 0:
        Arch = 32; // Intel x86
        result = false;
        break;
    case 6:
        result = false; // Itanium-based x64
        break;
    case 9:
        Arch = 64;
        result = true; // Intel/AMD x64
        break;
    default:
        result = false;
        break;
    }

    return result;
}

bool DisableWowRedirection()
{
    return Wow64DisableWow64FsRedirection(&OldWow64RedirectionValue);
}

bool RevertWowRedirection()
{
    return Wow64RevertWow64FsRedirection(OldWow64RedirectionValue);
}

bool CheckInstall()
{
    DWORD Code = 0;
    std::wstring TermServiceHost;
    CRegistry reg;

    //if (!reg.Open(TEXT("SYSTEM\\CurrentControlSet\\Services\\TermService")/*, Arch == 64 ? KEY_WOW64_64KEY : 0*/)) {
    if (!reg.Open(TEXT("SYSTEM\\CurrentControlSet\\Services\\TermService"), KEY_READ | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
        printf("[-] Open error (code %d).\n", GetLastError());
        return false;
    }

    if (!reg.Read(TEXT("ImagePath"), TermServiceHost)) {
        printf("[-] read error (code %d).\n", GetLastError());
        return false;
    }

    reg.Close();

    //%SystemRoot%\System32\svchost.exe -k NetworkService

    if (TermServiceHost.find(L"svchost.exe") == std::wstring::npos &&
        TermServiceHost.find(L"svchost -k") == std::wstring::npos) {
        printf("[-] TermService is hosted in a custom application (BeTwin, etc.) - unsupported.\n");
        printf("[*] ImagePath: %ws\n", TermServiceHost.c_str());
    }

    if (!reg.Open(HKEY_LOCAL_MACHINE, TEXT("SYSTEM\\CurrentControlSet\\Services\\TermService\\Parameters"), KEY_READ | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
        printf("[-] Open error (code %d).\n", GetLastError());
        return false;
    }

    //%SystemRoot%\System32\termsrv.dll
    if (!reg.Read(TEXT("ServiceDll"), TermServicePath)) {
        printf("[-] read error (code %d).\n", GetLastError());
        return false;
    }

    // #8: c_str() 返回 const 指针，强转写缓冲区是未定义行为；改用可变副本
    std::wstring TermServicePathLower = TermServicePath;
    _wcslwr_s(&TermServicePathLower[0], TermServicePathLower.size() + 1);
    TermServicePath.swap(TermServicePathLower);

    if (TermServicePath.find(L"termsrv.dll") == std::wstring::npos &&
        TermServicePath.find(L"rdpwrap.dll") == std::wstring::npos) {
        printf("[-] TermService is hosted in a custom application (BeTwin, etc.) - unsupported.\n");
        printf("[*] ImagePath: %ws\n", TermServicePath.c_str());
    }

    Installed = (TermServicePath.find(L"rdpwrap.dll") != std::wstring::npos);

    //Installed = (TermServicePath.find(L"\\system32\\termsrv.dll") != std::string::npos);
    // printf("[*] ServiceDll: %ws\n", TermServicePath.c_str());

    printf("[*] SuperRDP already installed? 【%s】\n", Installed ? "Yes!" : "No!");

    return true;
}

#include <vector>
#include <string>


//void SvcConfigStart(wchar_t* SvcName, DWORD StartType)
//{
//    printf("[*] Configuring %ws ...", SvcName);
//    SC_HANDLE hSC = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT);
//    if (hSC) {
//        SC_HANDLE hSvc = OpenService(hSC, SvcName, SERVICE_CHANGE_CONFIG);
//        if (hSvc) {
//            if (!ChangeServiceConfig(hSvc, SERVICE_NO_CHANGE, StartType,
//                SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL)) {
//                printf("[-] ChangeServiceConfig error (code %d).", GetLastError());
//            }
//
//            CloseServiceHandle(hSvc);
//        }
//        else {
//            printf("[-] OpenService error (code %d).", GetLastError());
//        }
//
//        CloseServiceHandle(hSC);
//    }
//    else {
//        printf("[-] OpenSCManager error (code %d).", GetLastError());
//    }
//}
//
//void SvcStart(wchar_t* SvcName)
//{
//    SC_HANDLE hSC = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT);
//    if (hSC) {
//        SC_HANDLE hSvc = OpenService(hSC, SvcName, SERVICE_START);
//        if (hSvc) {
//            if (!StartService(hSvc, 0, NULL)) {
//                if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING/*1056*/) {// Service already started
//                    Sleep(2000);// or SCM hasn't registered killed process
//                    if (!StartService(hSvc, 0, NULL)) {
//                        printf("[-] StartService error (code %d).", GetLastError());
//                    }
//                }
//                else {
//                    printf("[-] StartService error (code %d).", GetLastError());
//                }
//            }
//
//            CloseServiceHandle(hSvc);
//        }
//        else {
//            printf("[-] OpenService error (code %d).", GetLastError());
//        }
//
//        CloseServiceHandle(hSC);
//    }
//    else {
//        printf("[-] OpenSCManager error (code %d).", GetLastError());
//    }
//}


typedef struct
{
    union
    {
        struct
        {
            WORD Minor;
            WORD Major;
        } wVersion;
        DWORD dwVersion;
    };
    WORD Release;
    WORD Build;
} FILE_VERSION;

BOOL __stdcall GetFileVersion(LPCWSTR lptstrFilename, FILE_VERSION* FileVersion)
{
    typedef struct
    {
        WORD             wLength;
        WORD             wValueLength;
        WORD             wType;
        WCHAR            szKey[16];
        WORD             Padding1;
        VS_FIXEDFILEINFO Value;
        WORD             Padding2;
        WORD             Children;
    } VS_VERSIONINFO;

    HMODULE hFile = LoadLibraryExW(lptstrFilename, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!hFile)
    {
        return false;
    }

    HRSRC hResourceInfo = FindResourceW(hFile, (LPCWSTR)1, (LPCWSTR)0x10);
    if (!hResourceInfo)
    {
        FreeLibrary(hFile);
        return false;
    }

    HGLOBAL hResData = LoadResource(hFile, hResourceInfo);
    if (!hResData)
    {
        FreeLibrary(hFile);
        return false;
    }

    // 用 LockResource 取真实数据指针，并校验资源尺寸，避免截断/损坏的版本资源导致越界读
    VS_VERSIONINFO* VersionInfo = (VS_VERSIONINFO*)LockResource(hResData);
    if (!VersionInfo ||
        SizeofResource(hFile, hResourceInfo) < offsetof(VS_VERSIONINFO, Value) + sizeof(VS_FIXEDFILEINFO))
    {
        FreeLibrary(hFile);
        return false;
    }

    FileVersion->dwVersion = VersionInfo->Value.dwFileVersionMS;
    FileVersion->Release = (WORD)(VersionInfo->Value.dwFileVersionLS >> 16);
    FileVersion->Build = (WORD)VersionInfo->Value.dwFileVersionLS;

    // 版本数据已拷贝到出参，映射不再需要，成功路径也要释放
    FreeLibrary(hFile);
    return true;
}

bool CheckTermsrvVersion(wchar_t *IniPath)
{
    // 全局 IniFile 可能残留上一次的实例，先释放避免泄漏
    if (IniFile) {
        delete IniFile;
        IniFile = NULL;
    }

    // INI_FILE 构造函数在文件打开/读取失败时静默返回（SectionCount=0），
    // 导致后续 SectionExists 返回 false 时误报 "version not supported"。
    // 先检查文件是否存在且可读，给出准确的加载失败提示。
    DWORD attr = GetFileAttributesW(IniPath);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("[-] Failed to load configuration: INI file not found or inaccessible.\r\n");
        return false;
    }

    IniFile = new (std::nothrow) INI_FILE(IniPath);
    FILE_VERSION _FileVersion = { 0 };
    wchar_t termsrv[MAX_PATH] = { 0 };

    // TODO: implement this
    if (IniFile == NULL)
    {
        printf("[-] Failed to load configuration\r\n");
        return false;
    }


    if (!GetSystemDirectoryW(termsrv, MAX_PATH)) {
        printf("[-] GetSystemDirectory failed (code %d)\r\n", GetLastError());
        return false;
    }
    PathAppendW(termsrv, L"termsrv.dll");

    if (!GetFileVersion(termsrv, &_FileVersion)) {
        printf("[-] Failed to get termsrv.dll version\r\n");
        return false;
    }

    char Sect[256] = { 0 };
    wsprintfA(Sect, "%d.%d.%d.%d", _FileVersion.wVersion.Major, _FileVersion.wVersion.Minor, _FileVersion.Release, _FileVersion.Build);
    printf("[+] termsrv.dll %s\n", Sect);

    if (!IniFile->SectionExists(Sect))
    {
        printf("[-] Not support the version of termsrv, please contact author to update.\n");
        return false;
    }

    return true;
}

bool TSConfigFirewall(bool Enable)
{
    int ret;
    if (Enable) {
        // 规则名 "Remote Desktop" 为硬编码（原始设计，卸载按名删除不可改名）：
        // 先删既有同名规则（结果忽略），避免重复安装累积重复规则
        system("netsh advfirewall firewall delete rule name=\"Remote Desktop\"");
        ret = system("netsh advfirewall firewall add rule name=\"Remote Desktop\" dir=in protocol=tcp localport=3389 profile=any action=allow");
    }
    else {
        // 卸载时删除规则：规则不存在（非零返回）不算失败，卸载应为幂等
        ret = system("netsh advfirewall firewall delete rule name=\"Remote Desktop\"");
        if (ret != 0) {
            printf("[*] Firewall rule not found or already removed (code %d).\n", ret);
            return true;
        }
    }
    if (ret != 0) {
        printf("[-] netsh firewall config failed (code %d).\n", ret);
        return false;
    }
    return true;
}

//
//void KillProcess(DWORD pid)
//{
//    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, false, pid);
//    if (hProc) {
//        TerminateProcess(hProc, 0);
//        CloseHandle(hProc);
//    }
//}

bool TSConfigRegistry(bool Enable)
{
    CRegistry reg;
    if(!reg.Open(L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server", KEY_WRITE | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
        printf("[-] OpenKey error\n");
        return false;
    }

    if (!reg.Write(L"fDenyTSConnections", !Enable)) {
        printf("[-] writekey error\n");
        return false;
    }
    reg.Close();

    if (Enable) {
        if (!reg.CreateKey(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\Licensing Core", KEY_WRITE | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
            printf("[-] OpenKey error\n");
            //return false;
        }
        else {
            if (!reg.Write(L"EnableConcurrentSessions", true)) {
                printf("[-] writekey error\n");
                //return false;
            }
            reg.Close();
        }

        if (!reg.CreateKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", KEY_WRITE | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
            printf("[-] OpenKey error\n");
            //return false;
        }
        else {
            if (!reg.Write(L"AllowMultipleTSSessions", true)) {
                printf("[-] writekey error\n");
                //return false;
            }
            reg.Close();
        }

        // 原实现以 AddIns 父键存在性决定是否创建全部子键：父键存在即跳过，
        // 导致子键缺失或 Name/Type 错误时不会被修复。
        // 改为始终逐个 CreateKey（RegCreateKeyEx 对已存在的键等效于打开），确保每个子键的值都被正确写入。

        if (!reg.CreateKey(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\AddIns", KEY_WRITE | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
            printf("[-] OpenKey error\n");
            return false;
        }
        reg.Close();

        if (!reg.CreateKey(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\AddIns\\Clip Redirector", KEY_WRITE | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
            printf("[-] OpenKey error\n");
            return false;
        }
        if (!reg.Write(L"Name", L"RDPClip")) {
            printf("[-] writekey error\n");
            return false;
        }
        if (!reg.Write(L"Type", 3)) {
            printf("[-] writekey error\n");
            return false;
        }
        reg.Close();

        if (!reg.CreateKey(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\AddIns\\DND Redirector", KEY_WRITE | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
            printf("[-] OpenKey error\n");
            return false;
        }
        if (!reg.Write(L"Name", L"RDPDND")) {
            printf("[-] writekey error\n");
            return false;
        }
        if (!reg.Write(L"Type", 3)) {
            printf("[-] writekey error\n");
            return false;
        }
        reg.Close();

        if (!reg.CreateKey(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\AddIns\\Dynamic VC", KEY_WRITE | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
            printf("[-] OpenKey error\n");
            return false;
        }
        if (!reg.Write(L"Type", -1)) {
            printf("[-] writekey error\n");
            return false;
        }
    }

    return true;
}

bool SvcStart(wchar_t* SvcName)
{
    SC_HANDLE hSc = NULL;
    SC_HANDLE hSvc = NULL;
    bool ret = false;

    printf("[*] Starting %ws\n", SvcName);

    hSc = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT);
    if (hSc == NULL) {
        printf("OpenSCManager error : %d\n", GetLastError());
        goto  __exit;
    }

    hSvc = OpenService(hSc, SvcName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (hSvc == NULL) {
        printf("OpenService error : %d\n", GetLastError());
        goto __exit;
    }

    if (!StartService(hSvc, 0, NULL)) {
        int err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {// Service already started
            Sleep(2000);            // or SCM hasn't registered killed process
            if (!StartService(hSvc, 0, NULL)) {
                err = GetLastError();
                // 二次失败可能仍是 1056（SCM 状态滞后），查状态确认是否实际已运行，避免误判
                SERVICE_STATUS st = { 0 };
                bool running = (err == ERROR_SERVICE_ALREADY_RUNNING) &&
                    QueryServiceStatus(hSvc, &st) &&
                    (st.dwCurrentState == SERVICE_RUNNING);
                if (!running) {
                    printf("[*] Start service failed. %d\n", err);
                    goto __exit;
                }
            }
        }
        else if (err == 1062 /* ERROR_SERVICE_NOT_ACTIVE，MinGW 头文件未定义该宏 */) {
            // 刚终止过服务进程时 SCM 可能尚未同步停止状态，稍候重试一次
            Sleep(2000);
            if (!StartService(hSvc, 0, NULL)) {
                printf("[*] Start service failed. %d\n", GetLastError());
                goto __exit;
            }
        }
        else {
            printf("[*] Start service failed. %d\n", err);
            // 原实现落空到 ret = true，把启动失败误报为成功，必须走失败路径
            goto __exit;
        }
    }

    ret = true;

    printf("[*] Start service success.\n");

__exit:
    if (hSc) {
        CloseServiceHandle(hSc);
    }
    if (hSvc) {
        CloseServiceHandle(hSvc);
    }

    return ret;
}

void SvcConfigStart(const wchar_t* SvcName, DWORD dwStartType)
{
    SC_HANDLE hSc = NULL;
    SC_HANDLE hSvc = NULL;

    hSc = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT);
    if (hSc == NULL) {
        printf("OpenSCManager error : %d\n", GetLastError());
        goto  __exit;
    }

    hSvc = OpenService(hSc, SvcName, SERVICE_CHANGE_CONFIG);
    if (hSvc == NULL) {
        printf("OpenService error : %d\n", GetLastError());
        goto __exit;
    }

    if (!ChangeServiceConfig(hSvc, SERVICE_NO_CHANGE, dwStartType,
        SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL)) {
        printf("[-] ChangeServiceConfig error: %d\n", GetLastError());
    }

__exit:
    if (hSc) {
        CloseServiceHandle(hSc);
    }
    if (hSvc) {
        CloseServiceHandle(hSvc);
    }
}

int SvcGetStart(const wchar_t* SvcName)
{
    SC_HANDLE hSc = NULL;
    SC_HANDLE hSvc = NULL;
    DWORD pcbBytesNeeded = 0;
    QUERY_SERVICE_CONFIG* Buf = NULL;
    int ret = -1;   // 失败时返回 -1，避免与 SERVICE_BOOT_START(0) 混淆

    hSc = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT);
    if (hSc == NULL) {
        printf("OpenSCManager error : %d\n", GetLastError());
        goto  __exit;
    }

    hSvc = OpenService(hSc, SvcName, SERVICE_QUERY_CONFIG);
    if (hSvc == NULL) {
        printf("OpenService error : %d\n", GetLastError());
        goto __exit;
    }

    if (!QueryServiceConfig(hSvc, NULL, 0, &pcbBytesNeeded)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            printf("QueryServiceConfig error : %d\n", GetLastError());
            goto __exit;
        }
    }

    if (pcbBytesNeeded) {
        Buf = (QUERY_SERVICE_CONFIG*)new (std::nothrow) char[pcbBytesNeeded];
        if (Buf == NULL) {
            printf("no memory\n");
            goto __exit;
        }
        if (!QueryServiceConfig(hSvc, Buf, pcbBytesNeeded, &pcbBytesNeeded)) {
            printf("QueryServiceConfig error : %d\n", GetLastError());
            goto __exit;
        }

        ret = Buf->dwStartType;
    }

__exit:
    if (hSc) {
        CloseServiceHandle(hSc);
    }
    if (hSvc) {
        CloseServiceHandle(hSvc);
    }
    if (Buf) {
        delete[] (char*)Buf;   // 以 new char[] 分配，必须以 char* 释放，匹配元素类型
    }

    return ret;
}

void CheckTermsrvProcess()
{
    SC_HANDLE hSc = NULL;
    DWORD BytesNeeded = 0;
    DWORD ServicesReturned = 0;
    DWORD ResumeHandle = 0;
    ENUM_SERVICE_STATUS_PROCESS* Services = NULL;
    bool found = false;
    int i = 0;
    bool Started = false;
    bool again = false;
    int retry = 0;
__again:
    // 重试时声明在标签前的变量不会重新初始化，必须手动复位，
    // 否则上一轮的 found/again 残留会导致错误分支被跳过或重复循环
    found = false;
    again = false;
    ShareSvcCount = 0;  // 重试/多次调用均会重新枚举，必须重建列表，否则共享服务重复累积

    hSc = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    if (hSc == NULL) {
        printf("OpenSCManager error : %d\n", GetLastError());
        goto  __exit;
    }

    if (EnumServicesStatusEx(hSc,
        SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32,
        SERVICE_STATE_ALL,
        NULL,
        0,
        &BytesNeeded,
        &ServicesReturned,
        &ResumeHandle,
        NULL)) {
        // 正常应以 ERROR_MORE_DATA 失败并返回所需缓冲大小；返回 TRUE 仅在服务表为空时可能，给出明确提示
        printf("[-] EnumServicesStatusEx returned no data.\n");
        goto __exit;
    }

    if (GetLastError() != ERROR_MORE_DATA) {
        printf("EnumServicesStatusEx error : %d\n", GetLastError());
        goto __exit;
    }

    Services = (ENUM_SERVICE_STATUS_PROCESS *)new (std::nothrow) char[BytesNeeded];
    if (Services == NULL) {
        printf("no memory\n");
        goto __exit;
    }

    if (!EnumServicesStatusEx(hSc,
        SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32,
        SERVICE_STATE_ALL,
        (LPBYTE)Services,
        BytesNeeded,
        &BytesNeeded,
        &ServicesReturned,
        &ResumeHandle,
        NULL)) {
        printf("EnumServicesStatusEx error : %d\n", GetLastError());
        goto __exit;
    }

    //TermService
    for (i = 0; i < ServicesReturned; i++) {
        if (!_wcsicmp(Services[i].lpServiceName, TermService)) {
            TermServicePID = Services[i].ServiceStatusProcess.dwProcessId;
            found = true;
            break;
        }
    }

    if (!found) {
        printf("[-] TermService not found.\n");
        goto __exit;
    }

    if (TermServicePID != 0) {
        again = false;
        printf("[+] TermService found pid: %d\n", TermServicePID);
    }
    else {
        if (Started) {
            printf("Failed to set up TermService. Unknown error.\n");
            getchar();
            again = false;
            goto __exit;
        }
        // 原实现无重试上限：SvcStart 失败时 Started 保持 false，
        // again=true 反复跳回 __again，构成无限循环
        if (++retry > 3) {
            printf("[-] Failed to start TermService after %d retries.\n", retry - 1);
            again = false;
            goto __exit;
        }
        SvcConfigStart(TermService, SERVICE_AUTO_START);
        if (SvcStart(TermService)) {
            Started = true;
        }
        else {
            printf("Start TermService failed!\n");
        }

        // StartService 返回时服务可能仍处于 START_PENDING，dwProcessId 尚未就绪；
        // 必须等待服务过渡到 RUNNING 再重枚举，否则下一轮 PID=0 会直接报 "Unknown error" 放弃
        Sleep(2000);
        again = true;
        goto __exit;
    }

    for (i = 0; i < ServicesReturned; i++) {
        if (Services[i].lpServiceName) {
            if (Services[i].ServiceStatusProcess.dwProcessId == TermServicePID) {
                if (_wcsicmp(Services[i].lpServiceName, TermService)) {
                    // 数组大小 100，索引达到 100 即越界，必须 >= 判断
                    if (ShareSvcCount >= 100) {
                        printf("ShareSvcCount = %d\n", ShareSvcCount);
                        break;
                    }
                    StringCchCopyW(ShareSvc[ShareSvcCount++], MAX_PATH, Services[i].lpServiceName);
                    printf("[*] Shared services found: %ws\n", Services[i].lpServiceName);
                }
            }
        }
    }

__exit:
    if (Services) {
        delete[] (char*)Services;   // 以 new char[] 分配，必须以 char* 释放，匹配元素类型
        Services = NULL;
    }
    if (hSc) {
        CloseServiceHandle(hSc);
    }

    if (again) {
        goto __again;
    }
}

bool SetWrapperDll(wchar_t* path)
{
    CRegistry reg;
    //%SystemRoot%\System32\termsrv.dll
    if (!reg.Open(L"SYSTEM\\CurrentControlSet\\Services\\TermService\\Parameters", KEY_WRITE | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
        printf("[-] OpenKey error: %d\n", GetLastError());
        return false;
    }

    //必须是REG_EXPAND_SZ，否则找不到文件
    //必须放在system32,否则5
    //这是安装中唯一真正生效的写入，必须校验结果，否则杀进程后 ServiceDll 仍指向旧库
    if (!reg.WriteExpandSZ(L"ServiceDll", path)) {
        printf("[-] Write ServiceDll error: %d\n", GetLastError());
        return false;
    }
    return true;
    /*if (Arch == 64 && FV.Version.w.Major == 6 && FV.Version.w.Minor == 0) {
        system("reg.exe HKLM\SYSTEM\CurrentControlSet\Services\TermService\Parameters /v ServiceDll /t REG_EXPAND_SZ /d WrapPath /f")
    }*/
}
void CheckTermsrvDependencies()
{
    if (SvcGetStart(L"CertPropSvc") == SERVICE_DISABLED) {
        SvcConfigStart(L"CertPropSvc", SERVICE_DEMAND_START);
    }
    if (SvcGetStart(L"SessionEnv") == SERVICE_DISABLED) {
        SvcConfigStart(L"SessionEnv", SERVICE_DEMAND_START);
    }
}


bool KillProcess(DWORD pid)
{
    bool ret = false;

    // SYNCHRONIZE 用于终止后等待进程真正退出
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (hProc) {
        if (!TerminateProcess(hProc, 0)) {
            printf("[-] TerminateProcess error (code %d).\n", GetLastError());
        }
        else {
            // TerminateProcess 是异步的，等待进程实际退出，
            // 避免后续文件删除/覆盖及 StartService 出现竞态（如 1062）
            DWORD waitResult = WaitForSingleObject(hProc, 10000);
            if (waitResult == WAIT_OBJECT_0) {
                ret = true;
            }
            else {
                // 超时或失败：进程可能仍存活，后续文件操作可能失败
                printf("[-] Process did not exit within timeout (wait result %d).\n", waitResult);
            }
        }
        CloseHandle(hProc);
    }
    else {
        printf("[-] OpenProcess error (code %d).\n", GetLastError());
    }

    return ret;
}

int ReleaseFile(LPCTSTR path, LPCTSTR res_type, WORD res_id)
{  
    HRSRC   hrsc = FindResource(NULL, MAKEINTRESOURCE(res_id), res_type);
    if (hrsc == NULL) {
        return GetLastError();
    }
    HGLOBAL hG = LoadResource(NULL, hrsc);
    if (hG == NULL) {
        return GetLastError();
    }

    DWORD   dwSize = SizeofResource(NULL, hrsc);
    if (dwSize <= 0) {
        return ERROR_INVALID_FORM_SIZE;
    }

    // 创建文件  
    HANDLE  hFile = CreateFile(path,
        GENERIC_WRITE,
        FILE_SHARE_WRITE,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    // 写入文件：hG 仅是资源句柄，必须 LockResource 取真实数据指针，否则写出的是指针值/越界内存
    LPVOID pResData = LockResource(hG);
    if (pResData == NULL) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFile(path);   // 不留空壳文件，否则下次 PathFileExists 命中会跳过释放，损坏态永久残留
        return err;
    }
    DWORD dwWrite = 0;
    if (!WriteFile(hFile, pResData, dwSize, &dwWrite, NULL) || dwWrite != dwSize) {
        // 部分写入也会落盘残缺 dll，必须报错而非返回成功
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFile(path);   // 不留残缺文件，否则下次 PathFileExists 命中会跳过释放，损坏态永久残留
        return err != ERROR_SUCCESS ? err : ERROR_WRITE_FAULT;
    }
    CloseHandle(hFile);

    return ERROR_SUCCESS;
}

bool AddPrivilege(const wchar_t* SePriv);

bool InstallWrapper(wchar_t* wrapper)
{
    wchar_t rfxvmt[MAX_PATH] = { 0 };
    wchar_t rdpwrap[MAX_PATH] = { 0 };
    wchar_t dstini[MAX_PATH] = { 0 };
    wchar_t ini[MAX_PATH] = { 0 };   // 必须在首个 goto __exit 之前声明，否则跳转跨越初始化（仅靠 -fpermissive 才能编译）
    int ret = 0;
    bool ok = true;
    bool done = false;   // 是否走到末尾输出阶段（早期 goto 失败路径不算成功）
    DWORD sysDirLen = 0;  // 必须在首个 goto __exit 之前声明，避免 goto 跨越初始化

    if (Installed) {
        printf("[*] RDP Wrapper Library is already installed.\n");
        return true;   // 无需安装，幂等视为成功
    }

    if (Arch == 64) {
        // x64 进程中该 API 必然失败（仅对 WoW64 下的 32 位进程有意义），保留调用以兼容 32 位构建
        DisableWowRedirection();
    }

    // 后续所有失败路径统一 goto __exit，避免已禁用的 WoW64 重定向不回滚
    if (!PathFileExists(wrapper)) {
        printf("[*] RDP Wrapper Library not exists.\n");
        goto __exit;
    }

    StrCpyW(ini, wrapper);
    PathRemoveExtension(ini);
    StrCatW(ini, L".ini");

    if (!PathFileExists(ini)) {
        printf("[*] RDP Wrapper Library or config file not found.\n");
        goto __exit;
    }

    printf("[*] Installing...\n");

    if (!CheckTermsrvVersion(ini)) {
        goto __exit;
    }

    CheckTermsrvProcess();

    if (TermServicePID == 0) {
        printf("[*] Get TermService PID failed\n");
        goto __exit;
    }

    // 必须先终止服务进程再复制文件：更新流程中若旧 rdpwrap.dll 仍被服务进程加载，
    // CopyFileW 会遭遇 ERROR_SHARING_VIOLATION。先杀进程释放文件占用。
    printf("[*] Terminating service...\n");
    if (!AddPrivilege(L"SeDebugPrivilege")) {
        printf("[!] Enable SeDebugPrivilege failed, terminating service may fail.\n");
    }

    if (!KillProcess(TermServicePID)) {
        goto __exit;
    }

    Sleep(1000);

    printf("[*] Configuring service library...\n");

    // 必须校验返回值：返回 0 为失败；返回 >= MAX_PATH 表示缓冲不足（截断），后续 PathAppendW 会溢出
    sysDirLen = GetSystemDirectoryW(rfxvmt, MAX_PATH);
    if (sysDirLen == 0 || sysDirLen >= MAX_PATH) {
        printf("[-] GetSystemDirectory failed or path too long (code %d)\n", GetLastError());
        goto __exit;
    }
    wcscpy_s(rdpwrap, rfxvmt);
    wcscpy_s(dstini, rfxvmt);
    PathAppendW(rdpwrap, L"rdpwrap.dll");
    PathAppendW(dstini, L"rdpwrap.ini");

    //copy files
    PathAppendW(rfxvmt, L"rfxvmt.dll");
    if (!PathFileExistsW(rfxvmt)) {
        ret = ReleaseFile(rfxvmt, L"BIN", IDR_BIN1);
        if (ERROR_SUCCESS != ret) {
            printf("[-] Install rfxvmt failed: %d\n", ret);
            goto __exit;
        }
    }

    if (!CopyFileW(wrapper, rdpwrap, FALSE)) {
        printf("[-] Install rdpwrap.dll failed: %d\n", GetLastError());
        goto __exit;
    }

    if (!CopyFileW(ini, dstini, FALSE)) {
        printf("[-] Install rdpwrap.ini failed: %d\n", GetLastError());
        goto __exit;
    }

    if (!SetWrapperDll(rdpwrap)) {
        // ServiceDll 未写入成功：继续后续流程（重启后仍是旧库），但整体标记为失败
        ok = false;
    }

    printf("[*] Checking dependencies...\n");
    CheckTermsrvDependencies();

    for (int i = 0; i < ShareSvcCount; i++) {
        SvcStart(ShareSvc[i]);
    }

    Sleep(500);
    if (!SvcStart(TermService)) {
        printf("[-] TermService restart failed, please restart it manually.\n");
        ok = false;
    }
    Sleep(500);

    printf("[*] Configuring registry...\n");
    if (!TSConfigRegistry(true)) {
        ok = false;
    }
    printf("[*] Configuring firewall...\n");
    if (!TSConfigFirewall(true)) {
        ok = false;
    }

    done = true;
    if (ok) {
        printf("[+] Successfully installed.\n");
    }
    else {
        printf("[!] Installed with errors, please check the messages above.\n");
    }

__exit:

    if (Arch == 64) {
        RevertWowRedirection();
    }

    return done && ok;
}

bool AddPrivilege(const wchar_t* SePriv)
{
    HANDLE hToken = NULL;
    bool result = false;
    LUID luid = { 0 };

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        printf("[-] OpenProcessToken error (code %d).\n", GetLastError());
        goto __exit;
    }
   
    if (!LookupPrivilegeValue(NULL, SePriv, &luid)) {
        printf("[-] LookupPrivilegeValue error (code %d).\n", GetLastError());
        goto __exit;
    }

    TOKEN_PRIVILEGES tkp;
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Luid = luid;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL)) {
        printf("[-] AdjustTokenPrivileges error (code %d).\n", GetLastError());
    }
    else if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        printf("[-] AdjustTokenPrivileges: privilege not assigned (code %d).\n", GetLastError());
    }
    else {
        result = true;
    }

__exit:
    if (hToken) {
        CloseHandle(hToken);
    }    

    return result;
}


std::wstring ExpandPath(const wchar_t *Path)
{
    wchar_t fullpath[MAX_PATH] = { 0 };
    // 返回 0 为失败；返回 > MAX_PATH 表示缓冲不足（此时缓冲内容不可用），均返回空串
    DWORD ret = ExpandEnvironmentStrings(Path, fullpath, MAX_PATH);
    if (ret == 0 || ret > MAX_PATH) {
        return std::wstring();
    }
    return std::wstring(fullpath);
}

void DeleteFiles()
{
    std::wstring FullPath = ExpandPath(TermServicePath.c_str());
    if (FullPath.empty()) {
        printf("[-] Expand ServiceDll path failed, skip file removal.\n");
        return;
    }
    // 注册表值长度不受 MAX_PATH 限制，超长时 wcscpy_s 会触发 invalid parameter 直接 abort
    if (FullPath.length() >= MAX_PATH) {
        printf("[-] ServiceDll path too long, skip file removal.\n");
        return;
    }
    wchar_t Path[MAX_PATH] = { 0 };
    wchar_t Name[MAX_PATH] = { 0 };
    wcscpy_s(Path, FullPath.c_str());
    PathRemoveFileSpec(Path);
    wcscpy_s(Name, PathFindFileName(FullPath.c_str()));
    PathRemoveExtension(Name);
    wchar_t IniName[MAX_PATH] = { 0 };
    wcscpy_s(IniName, Name);
    wcscat_s(IniName, L".ini");
    PathAppend(Path, IniName);

    if (DeleteFile(Path)) {
        printf("[+] Removed file: %ws\n", Path);
    }
    else {
        printf("[-] DeleteFile error (code %d).\n", GetLastError());
    }

    if (DeleteFile(FullPath.c_str())) {
        printf("[+] Removed file: %ws\n", FullPath.c_str());
    }
    else {
        printf("[-] DeleteFile error (code %d).\n", GetLastError());
    }

}

bool UninstallWrapper()
{
    bool result = false;
    bool ok = true;   // 聚合服务重启/注册表/防火墙等后续步骤结果，必须声明在首个 goto 之前
    CRegistry reg;

    if (!Installed) {
        printf("[*] RDP Wrapper Library is not installed.\n");
        return true;   // 无需卸载，幂等视为成功
    }
    printf("[*] Uninstalling...\n");

    if (Arch == 64 ){
        // x64 进程中该 API 必然失败（仅对 WoW64 下的 32 位进程有意义），保留调用以兼容 32 位构建
        DisableWowRedirection();
    }

    CheckTermsrvProcess();

    printf("[*] Resetting service library...\n");

    //%SystemRoot%\System32\termsrv.dll
    if (!reg.Open(TEXT("SYSTEM\\CurrentControlSet\\Services\\TermService\\Parameters"), KEY_WRITE | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
        printf("[-] Open error (code %d).\n", GetLastError());
        goto __exit;
    }

    if (!reg.WriteExpandSZ(TEXT("ServiceDll"), L"%SystemRoot%\\System32\\termsrv.dll")) {
        printf("[-] write error (code %d).\n", GetLastError());
        goto __exit;
    }

    printf("[*] Terminating service...\n");

    AddPrivilege(L"SeDebugPrivilege");

    // 服务未运行（PID=0）时无需终止；否则 OpenProcess(0) 必然失败导致整个卸载中止，文件无法删除
    if (TermServicePID != 0) {
        if (!KillProcess(TermServicePID)) {
            goto __exit;
        }
        Sleep(1000);
    }
    else {
        printf("[*] TermService is not running, skip terminating.\n");
    }

    printf("[*] Removing files...\n");

    DeleteFiles();

    for (int i = 0; i < ShareSvcCount; i++) {
        SvcStart(ShareSvc[i]);
    }

    Sleep(500);
    // 原实现忽略以下全部步骤的返回值，存在假成功；与安装流程一致进行聚合
    if (!SvcStart(TermService)) {
        printf("[-] TermService restart failed, please restart it manually.\n");
        ok = false;
    }
    Sleep(500);

    printf("[*] Configuring registry...\n");
    if (!TSConfigRegistry(FALSE)) {
        ok = false;
    }
    printf("[*] Configuring firewall...\n");
    if (!TSConfigFirewall(FALSE)) {
        ok = false;
    }

    result = ok;

__exit:
    reg.Close();

    if (Arch == 64) {
        RevertWowRedirection();
    }

    if (result) {
        Installed = FALSE;
        printf("[+] Successfully uninstalled.\n");
    }
    else {
        printf("[-] Uninstall failed.\n");
    }

    return result;
}

bool ForceRestartTerminalService()
{
    bool ok = true;

    printf("[*] Restarting...\n");

    CheckTermsrvProcess();

    printf("[*] Terminating service...\n");
    AddPrivilege(L"SeDebugPrivilege");
    // 服务未运行时跳过终止，避免 OpenProcess(0) 报错刷屏
    if (TermServicePID != 0) {
        KillProcess(TermServicePID);
        Sleep(1000);
    }
    else {
        printf("[*] TermService is not running.\n");
    }

    for (int i = 0; i < ShareSvcCount; i++) {
        SvcStart(ShareSvc[i]);
    }

    Sleep(500);
    if (!SvcStart(TermService)) {
        printf("[-] TermService restart failed, please restart it manually.\n");
        ok = false;
    }

    if (ok) {
        printf("[+] Done.\n");
    }
    return ok;
}


int wmain(int argc, wchar_t* argv[])
{
    char option[20] = { 0 };
    wchar_t rdpwrap[MAX_PATH] = { 0 };
    bool update = false;
    int exitCode = 1;   // 悲观默认：仅在流程明确成功时置 0，供 GUI 判定成败

    printf("usage: SuperRDP.exe update\n");
    printf("\tupdate: uninstall old version and resintall new version\n\n");

    if (argc == 2 && !_wcsicmp(argv[1], L"update")) {
        update = true;
    }

    printf("[+] SuperRDP initialize...\n\n");

    if (!SupportedArchitecture()) {
        printf("[-] Unsupported processor architecture. Only for Arch (%d)\n", Arch);
        goto __exit;
    }

    if (!CheckInstall()) {
        printf("[-] No term service\n");
        goto __exit;
    }

    printf("\n[+] SuperRDP initialize success...\n\n");

    printf("--------------------------------------------------------\n\n");


    if (update) {
        printf("[+] checked update option, SupreRDP will automatically update.\n");

        if (Installed) {
            printf("[+] do option 2, uninstall...\n");
            UninstallWrapper();
            if (Installed) {
                // 卸载未完全成功也必须清除安装标记，否则 InstallWrapper 会以
                // "already installed" 早退，导致 update 静默变成空操作
                printf("[!] Uninstall incomplete, forcing reinstall...\n");
                Installed = false;
            }
        }

        printf("[+] do option 1, install...\n");

        GetModuleFileNameW(NULL, rdpwrap, MAX_PATH);
        PathRemoveFileSpecW(rdpwrap);
        PathAppendW(rdpwrap, L"rdpwrap.dll");
        if (!PathFileExistsW(rdpwrap)) {
            printf("[-] Can't found rdpwrap.dll, please download the file from https://github.com/sky12378/SuperRDP2-new\n");
            goto __exit;
        }

        exitCode = InstallWrapper(rdpwrap) ? 0 : 1;
        printf("[*] Update flow finished.\n");

        goto __exit;
    }


    printf("Please select option:\n");
    printf("    1: Install SuperRDP to Program Files folder (default)\n");
    printf("    2: Uninstall SuperRDP\n");
    printf("    3: Force restart Terminal Services\n\n");

    printf("> ");
    fflush(stdin);//清除输入（标准上对输入流 fflush 是 UB，Windows 控制台实现有效）
    option[0] = '\0';
    scanf("%1s", option);

    if (option[0] == '1') {

        printf("[+] Select option 1, install...\n");

        GetModuleFileNameW(NULL, rdpwrap, MAX_PATH);
        PathRemoveFileSpecW(rdpwrap);
        PathAppendW(rdpwrap, L"rdpwrap.dll");
        if (!PathFileExistsW(rdpwrap)) {
            printf("[-] Can't found rdpwrap.dll, please download the file from https://github.com/sky12378/SuperRDP2-new\n");
            goto __exit;
        }

        exitCode = InstallWrapper(rdpwrap) ? 0 : 1;
    }
    else if (option[0] == '2') {
        printf("[+] Select option 2, uninstall...\n");
        exitCode = UninstallWrapper() ? 0 : 1;
    }
    else if (option[0] == '3') {
        printf("[+] Select option 3, force restart services...\n");
        exitCode = ForceRestartTerminalService() ? 0 : 1;
    }
    else {
        printf("Invalid option.\n");
    }

__exit:

    if (IniFile) {
        delete IniFile;
        IniFile = NULL;
    }

    system("pause");

    return exitCode;

}

//-i D:\SrcCode\RDPWarp_c\RDPWrap\Debug\RDPWrap.dll