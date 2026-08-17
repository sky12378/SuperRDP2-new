// Registry.cpp : implementation file
//

#include "pch.h"
#include "Registry.h"
#include <assert.h>
#include <tchar.h>
#pragma comment(lib, "advapi32.lib")
/////////////////////////////////////////////////////////////////////////////
// CRegistry

CRegistry::CRegistry(HKEY hKey)
{
    m_hKey=hKey;
}

CRegistry::~CRegistry()
{
    Close();
}

/////////////////////////////////////////////////////////////////////////////
// CRegistry Functions

// 预定义句柄（hive 根）不可 RegCloseKey：单参重载里 m_hKey 可能是
// 双参重载刚赋值的 HKEY_LOCAL_MACHINE 等，只有非预定义句柄才能关闭
static bool IsPredefinedHive(HKEY h)
{
    return h == HKEY_LOCAL_MACHINE || h == HKEY_CURRENT_USER ||
           h == HKEY_CLASSES_ROOT || h == HKEY_USERS ||
           h == HKEY_CURRENT_CONFIG || h == HKEY_PERFORMANCE_DATA;
}

BOOL CRegistry::CreateKey(LPCTSTR lpSubKey, DWORD Flag/* = 0*/)
{
    assert(m_hKey);
    assert(lpSubKey);

    // 若当前持有的是上次 Open/CreateKey 的实句柄，先关闭避免泄漏
    if (!IsPredefinedHive(m_hKey)) Close();

    HKEY hKey;
    DWORD dw;
    long lReturn=RegCreateKeyEx(m_hKey,lpSubKey,0L,NULL,REG_OPTION_NON_VOLATILE,/*KEY_ALL_ACCESS|*/Flag,NULL,&hKey,&dw);
   
    if(lReturn==ERROR_SUCCESS)
    {
        m_hKey=hKey;
        return TRUE;
    }
   
    return FALSE;
   
}

BOOL CRegistry::CreateKey(HKEY root, LPCTSTR lpSubKey, DWORD Flag/* = 0*/)
{
    assert(root);
    assert(lpSubKey);

    // 先关闭之前打开的句柄，防止调用者遗漏 Close 导致资源泄漏
    Close();
    m_hKey = root;
    return CreateKey(lpSubKey, Flag);
}

BOOL CRegistry::Open(LPCTSTR lpSubKey, DWORD Flag /*= 0*/)
{
    assert(m_hKey);
    assert(lpSubKey);

    // 若当前持有的是上次 Open/CreateKey 的实句柄，先关闭避免泄漏
    if (!IsPredefinedHive(m_hKey)) Close();
   
    HKEY hKey;
    long lReturn=RegOpenKeyEx(m_hKey,lpSubKey,0L,/*KEY_ALL_ACCESS | Flag*/Flag,&hKey);
   
    if(lReturn==ERROR_SUCCESS)
    {
        m_hKey=hKey;
        return TRUE;
    }
    return FALSE;
   
}

BOOL CRegistry::Open(HKEY root, LPCTSTR lpSubKey, DWORD Flag)
{
    assert(root);
    assert(lpSubKey);
    // 先关闭之前打开的句柄，防止调用者遗漏 Close 导致资源泄漏
    Close();
    m_hKey = root;

    return Open(lpSubKey, Flag);
}

void CRegistry::Close()
{
    // 预定义 hive 句柄（HKEY_LOCAL_MACHINE 等）不可 RegCloseKey：Open 失败时
    // m_hKey 仍是构造时赋的预定义句柄，直接关闭会导致同进程后续 HKLM 操作异常
    if(m_hKey && !IsPredefinedHive(m_hKey))
    {
        RegCloseKey(m_hKey);
    }
    m_hKey=NULL;
}

BOOL CRegistry::DeleteValue(LPCTSTR lpValueName)
{
    assert(m_hKey);
    assert(lpValueName);
   
    long lReturn=RegDeleteValue(m_hKey,lpValueName);
   
    if(lReturn==ERROR_SUCCESS)
        return TRUE;
    return FALSE;
   
}

BOOL CRegistry::DeleteKey(HKEY hKey, LPCTSTR lpSubKey)
{
    assert(hKey);
    assert(lpSubKey);
   
    long lReturn=RegDeleteKey(hKey,lpSubKey);
   
    if(lReturn==ERROR_SUCCESS)
        return TRUE;
    return FALSE;
   
}

BOOL CRegistry::Write(LPCTSTR lpSubKey, int nVal)
{
    assert(m_hKey);
    assert(lpSubKey);
   
    DWORD dwValue;
    dwValue=(DWORD)nVal;
   
    long lReturn=RegSetValueEx(m_hKey,lpSubKey,0L,REG_DWORD,(const BYTE *) &dwValue,sizeof(DWORD));
   
       if(lReturn==ERROR_SUCCESS)
        return TRUE;
   
    return FALSE;
   
}

BOOL CRegistry::Write(LPCTSTR lpSubKey, DWORD dwVal)
{
    assert(m_hKey);
    assert(lpSubKey);
   
    long lReturn=RegSetValueEx(m_hKey,lpSubKey,0L,REG_DWORD,(const BYTE *) &dwVal,sizeof(DWORD));
   
       if(lReturn==ERROR_SUCCESS)
        return TRUE;
   
    return FALSE;
   
}

BOOL CRegistry::Write(LPCTSTR lpValueName, LPCTSTR lpValue)
{
    assert(m_hKey);
    assert(lpValueName);
    assert(lpValue);  

    long lReturn=RegSetValueEx(m_hKey,lpValueName,0L,REG_SZ,(const BYTE *) lpValue, _tcslen(lpValue)*sizeof(TCHAR)+sizeof(TCHAR));
   
       if(lReturn==ERROR_SUCCESS)
        return TRUE;
   
    return FALSE;
   
}

BOOL CRegistry::WriteExpandSZ(LPCTSTR lpValueName, LPCTSTR lpValue)
{
    assert(m_hKey);
    assert(lpValueName);
    assert(lpValue);

    long lReturn = RegSetValueEx(m_hKey, lpValueName, 0L, REG_EXPAND_SZ, (const BYTE *)lpValue, _tcslen(lpValue) * sizeof(TCHAR) + sizeof(TCHAR));

    if (lReturn == ERROR_SUCCESS)
        return TRUE;

    return FALSE;
}

BOOL CRegistry::Read(LPCTSTR lpValueName, int* pnVal)
{
    assert(m_hKey);
    assert(lpValueName);
    assert(pnVal);
   
    DWORD dwType;
    DWORD dwSize=sizeof(DWORD);
    DWORD dwDest;
    long lReturn=RegQueryValueEx(m_hKey,lpValueName,NULL,&dwType,(BYTE *)&dwDest,&dwSize);
   
    if(lReturn==ERROR_SUCCESS)
    {
        *pnVal=(int)dwDest;
        return TRUE;
    }
    return FALSE;
   
}

BOOL CRegistry::Read(LPCTSTR lpValueName, DWORD* pdwVal)
{
    assert(m_hKey);
    assert(lpValueName);
    assert(pdwVal);
   
    DWORD dwType;
    DWORD dwSize=sizeof(DWORD);
    DWORD dwDest;
    long lReturn=RegQueryValueEx(m_hKey,lpValueName,NULL,&dwType,(BYTE *)&dwDest,&dwSize);
   
    if(lReturn==ERROR_SUCCESS)
    {
        *pdwVal=dwDest;
        return TRUE;
    }
    return FALSE;
   
}

BOOL CRegistry::RestoreKey(LPCTSTR lpFileName)
{
    assert(m_hKey);
    assert(lpFileName);
   
    long lReturn=RegRestoreKey(m_hKey,lpFileName,REG_WHOLE_HIVE_VOLATILE);
   
    if(lReturn==ERROR_SUCCESS)
        return TRUE;
   
    return FALSE;
}

BOOL CRegistry::SaveKey(LPCTSTR lpFileName)
{
    assert(m_hKey);
    assert(lpFileName);
   
    long lReturn=RegSaveKey(m_hKey,lpFileName,NULL);
   
    if(lReturn==ERROR_SUCCESS)
        return TRUE;
   
    return FALSE;
}

//BOOL CRegistry::Read(LPCTSTR lpValueName, CString* lpVal)
//{
//    assert(m_hKey);
//    assert(lpValueName);
//    assert(lpVal);
//   
//    DWORD dwType;
//    DWORD dwSize=200;
//    char szString[2550];
//   
//    long lReturn=RegQueryValueEx(m_hKey,lpValueName,NULL,&dwType,(BYTE *)szString,&dwSize);
//   
//    if(lReturn==ERROR_SUCCESS)
//    {
//        *lpVal=szString;
//        return TRUE;
//    }
//    return FALSE;
//   
//}

BOOL CRegistry::Read(LPCTSTR lpValueName, std::string& lpVal)
{
    assert(m_hKey);
    assert(lpValueName);

    DWORD dwType;
    // 第一次调用：查询所需大小
    DWORD dwSize = 0;
    long lReturn = RegQueryValueEx(m_hKey, lpValueName, NULL, &dwType, NULL, &dwSize);
    if (lReturn != ERROR_SUCCESS && lReturn != ERROR_MORE_DATA)
    {
        return FALSE;
    }
    if (dwSize == 0)
    {
        lpVal.clear();
        return TRUE;
    }

    // 第二次调用：实际读取，动态分配缓冲区
    BYTE* pBuffer = new BYTE[dwSize];
    if (!pBuffer) return FALSE;
    lReturn = RegQueryValueEx(m_hKey, lpValueName, NULL, &dwType, pBuffer, &dwSize);

    if (lReturn == ERROR_SUCCESS)
    {
        std::string str(reinterpret_cast<const char*>(pBuffer), dwSize);
        lpVal.swap(str);
        delete[] pBuffer;
        return TRUE;
    }
    delete[] pBuffer;
    return FALSE;

}

BOOL CRegistry::Read(LPCTSTR lpValueName, std::wstring& lpVal)
{
    assert(m_hKey);
    assert(lpValueName);

    DWORD dwType;
    // 第一次调用：查询所需大小
    DWORD dwSize = 0;
    long lReturn = RegQueryValueEx(m_hKey, lpValueName, NULL, &dwType, NULL, &dwSize);
    if (lReturn != ERROR_SUCCESS && lReturn != ERROR_MORE_DATA)
    {
        return FALSE;
    }
    if (dwType != REG_SZ && dwType != REG_EXPAND_SZ && dwType != REG_MULTI_SZ)
    {
        return FALSE;
    }
    if (dwSize == 0)
    {
        lpVal.clear();
        return TRUE;
    }

    // 第二次调用：实际读取，动态分配缓冲区
    BYTE* pBuffer = new BYTE[dwSize + sizeof(wchar_t)]; // +1 确保终止符安全
    if (!pBuffer) return FALSE;
    memset(pBuffer, 0, dwSize + sizeof(wchar_t));
    lReturn = RegQueryValueEx(m_hKey, lpValueName, NULL, &dwType, pBuffer, &dwSize);

    if (lReturn == ERROR_SUCCESS)
    {
        // dwSize 是字节数，wstring 构造按 wchar_t 计数；注意可能末尾未带终止符
        size_t charCount = dwSize / sizeof(wchar_t);
        // 若最后一个是终止符则剔除，避免 wstring 里含多余 NUL
        if (charCount > 0 && reinterpret_cast<const wchar_t*>(pBuffer)[charCount - 1] == L'\0')
        {
            charCount--;
        }
        std::wstring str(reinterpret_cast<const wchar_t*>(pBuffer), charCount);
        lpVal.swap(str);
        delete[] pBuffer;
        return TRUE;
    }
    delete[] pBuffer;
    return FALSE;

}