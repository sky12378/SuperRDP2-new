
#include "pch.h"
#include <stdio.h>
#include <iostream>
#include <windows.h>
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
wchar_t LogFile[MAX_PATH] = { 0 };


//int SC_ENUM_PROCESS_INFO = 0;
wchar_t TermService[] = L"TermService";
bool Installed = false;
wchar_t WrapPath[MAX_PATH] = { 0 };
char IniPath[] = { 0 };
int Arch = 0;
PVOID OldWow64RedirectionValue = NULL;
std::wstring TermServicePath;
//FILE_VERSION FV = { 0 }; //
DWORD TermServicePID = 0;
wchar_t TermServiceName[MAX_PATH] = { 0 };
wchar_t ShareSvc[100][MAX_PATH] = { 0 };
int ShareSvcCount = 0;
char sShareSvc[1];

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

    Installed = (TermServicePath.find(L"rdpwrap.dll") != std::string::npos);

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
        return false;
    }

    VS_VERSIONINFO* VersionInfo = (VS_VERSIONINFO*)LoadResource(hFile, hResourceInfo);
    if (!VersionInfo)
    {
        return false;
    }

    FileVersion->dwVersion = VersionInfo->Value.dwFileVersionMS;
    FileVersion->Release = (WORD)(VersionInfo->Value.dwFileVersionLS >> 16);
    FileVersion->Build = (WORD)VersionInfo->Value.dwFileVersionLS;

    return true;
}

bool CheckTermsrvVersion(wchar_t *IniPath)
{
    // 全局 IniFile 可能残留上一次的实例，先释放避免泄漏
    if (IniFile) {
        delete IniFile;
        IniFile = NULL;
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


    GetSystemDirectoryW(termsrv, MAX_PATH);
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

void TSConfigFirewall(bool Enable)
{
    if (Enable) {
        system("netsh advfirewall firewall add rule name=\"Remote Desktop\" dir=in protocol=tcp localport=3389 profile=any action=allow");
    }
    else {
        system("netsh advfirewall firewall delete rule name=\"Remote Desktop\"");
    }
}

bool EnableDebugPrivilege()
{
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
    {
        LUID luid;
        LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid);
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
        // AdjustTokenPrivileges 可能返回 TRUE 但实际未全部授予，需再查 GetLastError
        bool ok = (GetLastError() == ERROR_SUCCESS);
        CloseHandle(hToken);
        return ok;
    }
    return FALSE;
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

        CRegistry reg1;
        
        if (!reg1.Open(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\AddIns", KEY_READ | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {

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

    hSvc = OpenService(hSc, SvcName, SERVICE_START);
    if (hSvc == NULL) {
        printf("OpenService error : %d\n", GetLastError());
        goto __exit;
    }

    if (!StartService(hSvc, 0, NULL)) {
        int err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {// Service already started
            Sleep(2000);            // or SCM hasn't registered killed process
            if (!StartService(hSvc, 0, NULL)) {
                goto __exit;
            }
            else {

            }
        }
        else {
            printf("[*] Start service faild.%d\n", err);
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
        printf("[-] ChangeServiceConfig error: %d", GetLastError());
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
    int ret = 0;

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
        Buf = (QUERY_SERVICE_CONFIG*)new char[pcbBytesNeeded];
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
        delete[] Buf;
    }

    return ret;
}

void CheckTermsrvProcess()
{
    SC_HANDLE hSc = NULL;
    SC_HANDLE hSvc = NULL;
    DWORD BytesNeeded = 0;
    DWORD ServicesReturned = 0;
    DWORD ResumeHandle = 0;
    ENUM_SERVICE_STATUS_PROCESS* Services = NULL;
    bool found = false;
    int i = 0;
    bool Started = false;
    bool again = false;
__again:

    hSc = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    if (hSc == NULL) {
        printf("OpenSCManager error : %d\n", GetLastError());
        goto  __exit;
    }
   
    if (!EnumServicesStatusEx(hSc,
        SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32,
        SERVICE_STATE_ALL,
        NULL,
        0,
        &BytesNeeded,
        &ServicesReturned,
        &ResumeHandle,
        NULL)) {
        if (GetLastError() != ERROR_MORE_DATA) {
            printf("EnumServicesStatusEx error : %d\n", GetLastError());
            goto __exit;
        }

        Services = (ENUM_SERVICE_STATUS_PROCESS *)new char[BytesNeeded];
        if (Services == NULL) {
            printf("new error : %d\n", GetLastError());
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
                StrCpy(TermServiceName, Services[i].lpServiceName);
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
            SvcConfigStart(TermService, SERVICE_AUTO_START);
            if (SvcStart(TermService)) {
                Started = true;
            }
            else {
                printf("Start TermService failed!\n");
            }
            
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
                        StrCpy(ShareSvc[ShareSvcCount++], Services[i].lpServiceName);
                        printf("[*] Shared services found: %ws\n", Services[i].lpServiceName);
                    }
                }
            }
        }
    }

__exit:
    if (Services) {
        delete[] Services;
        Services = NULL;
    }
    if (hSc) {
        CloseServiceHandle(hSc);
    }
    if (hSvc) {
        CloseServiceHandle(hSvc);
    }

    if (again) {
        goto __again;
    }
}

void SetWrapperDll(wchar_t* path)
{
    CRegistry reg;
    //%SystemRoot%\System32\termsrv.dll
    if (!reg.Open(L"SYSTEM\\CurrentControlSet\\Services\\TermService\\Parameters", KEY_WRITE | (Arch == 64 ? KEY_WOW64_64KEY : 0))) {
        printf("[-] OpenKey error: %d\n", GetLastError());
        return;
    }

    //必须是REG_EXPAND_SZ，否则找不到文件
    //必须放在system32,否则5
    reg.WriteExpandSZ(L"ServiceDll", path);
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

    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProc) {
        if (!TerminateProcess(hProc, 0)) {
            printf("[-] TerminateProcess error (code %d).\n", GetLastError());
        }
        else {
            ret = true;
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
        CloseHandle(hFile);
        return GetLastError();
    }
    DWORD dwWrite = 0;
    WriteFile(hFile, pResData, dwSize, &dwWrite, NULL);
    CloseHandle(hFile);

    return ERROR_SUCCESS;
}

void InstallWrapper(wchar_t* wrapper)
{
    wchar_t rfxvmt[MAX_PATH] = { 0 };
    wchar_t rdpwrap[MAX_PATH] = { 0 };
    wchar_t dstini[MAX_PATH] = { 0 };
    wchar_t ini[MAX_PATH] = { 0 };   // 必须在首个 goto __exit 之前声明，否则跳转跨越初始化（仅靠 -fpermissive 才能编译）
    int ret = 0;

    if (Installed) {
        printf("[*] RDP Wrapper Library is already installed.\n");
        return;
    }

    if (Arch == 64) {
        DisableWowRedirection();
    }

    // 后续所有失败路径统一 goto __exit，避免已禁用的 WoW64 重定向不回滚
    if (!PathFileExists(wrapper)) {
        printf("[*] RDP Wrapper Library not exists.\n");
        goto __exit;
    }

    StrCpy(WrapPath, wrapper);

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

    printf("[*] Configuring service library...\n");

    GetSystemDirectoryW(rfxvmt, MAX_PATH);
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
        printf("[-] Install rfxvmt failed: %d\n", GetLastError());
        goto __exit;
    }

    if (!CopyFileW(ini, dstini, FALSE)) {
        printf("[-] Install rfxvmt failed: %d\n", GetLastError());
        goto __exit;
    }

    SetWrapperDll(rdpwrap);

    printf("[*] Checking dependencies...\n");
    CheckTermsrvDependencies();

    printf("[*] Terminating service...\n");
    EnableDebugPrivilege();

    if (!KillProcess(TermServicePID)) {
        goto __exit;
    }

    Sleep(1000);

    for (int i = 0; i < ShareSvcCount; i++) {
        SvcStart(ShareSvc[i]);
    }

    Sleep(500);
    SvcStart(TermService);
    Sleep(500);

    printf("[*] Configuring registry...\n");
    TSConfigRegistry(true);
    printf("[*] Configuring firewall...\n");
    TSConfigFirewall(true);

    printf("[+] Successfully installed.\n");

__exit:

    if (Arch == 64) {
        RevertWowRedirection();
    }
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
    ExpandEnvironmentStrings(Path, fullpath, MAX_PATH);
    return std::wstring(fullpath);
}

void DeleteFiles()
{
    std::wstring FullPath = ExpandPath(TermServicePath.c_str());
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

void UninstallWrapper()
{
    bool result = false;
    CRegistry reg;

    if (!Installed) {
        printf("[*] RDP Wrapper Library is not installed.\n");
        return;
    }
    printf("[*] Uninstalling...\n");

    if (Arch == 64 ){
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
        printf("[-] read error (code %d).\n", GetLastError());
        goto __exit;
    }

    printf("[*] Terminating service...\n");

    AddPrivilege(L"SeDebugPrivilege");

    if (!KillProcess(TermServicePID)) {
        goto __exit;
    }

    Sleep(1000);

    printf("[*] Removing files...\n");

    DeleteFiles();

    for (int i = 0; i < ShareSvcCount; i++) {
        SvcStart(ShareSvc[i]);
    }

    Sleep(500);
    SvcStart(TermService);
    Sleep(500);

    printf("[*] Configuring registry...\n");
    TSConfigRegistry(FALSE);
    printf("[*] Configuring firewall...\n");
    TSConfigFirewall(FALSE);

    result = true;

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
    
}

void ForceRestartTerminalService()
{
    printf("[*] Restarting...\n");

    CheckTermsrvProcess();

    printf("'[*] Terminating service...\n");
    AddPrivilege(L"SeDebugPrivilege");
    KillProcess(TermServicePID);
    Sleep(1000);

    for (int i = 0; i < ShareSvcCount; i++) {
        SvcStart(ShareSvc[i]);
    }

    Sleep(500);
    SvcStart(TermService);

    printf("[+] Done.\n");
}


int wmain(int argc, wchar_t* argv[])
{
    char option[20] = { 0 };
    wchar_t rdpwrap[MAX_PATH] = { 0 };
    bool update = false;

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
        }

        printf("[+] do option 1, install...\n");

        GetModuleFileNameW(NULL, rdpwrap, MAX_PATH);
        PathRemoveFileSpecW(rdpwrap);
        PathAppendW(rdpwrap, L"rdpwrap.dll");
        if (!PathFileExistsW(rdpwrap)) {
            printf("[-] Can't found rdpwrap.dll, please download the file from https://github.com/sky12378/SuperRDP2-new\n");
            goto __exit;
        }

        InstallWrapper(rdpwrap);
        printf("[+] done.\n");

        goto __exit;
    }


    printf("Please select option:\n");
    printf("    1: Install SuperRDP to Program Files folder (default)\n");
    printf("    2: Uninstall SuperRDP\n");
    printf("    3: Force restart Terminal Services\n\n");

    printf("> ");
    fflush(stdin);//清除输入
    option[0] = L'\0';
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

        InstallWrapper(rdpwrap);
    }
    else if (option[0] == '2') {
        printf("[+] Select option 2, uninstall...\n");
        UninstallWrapper();
    }
    else if (option[0] == '3') {
        printf("[+] Select option 3, force restart services...\n");
        ForceRestartTerminalService();
    }
    else {
        printf("Invalid option.\n");
    }

__exit:

    system("pause");

    return 0;

}

//-i D:\SrcCode\RDPWarp_c\RDPWrap\Debug\RDPWrap.dll