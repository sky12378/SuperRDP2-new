# SuperRDP2-new

**中文** | [English](README.en.md)

解锁 Windows Home / 非 Pro 版本被阉割的**远程桌面（RDP）**功能 —— 基于 [anhkgg/SuperRDP](https://github.com/anhkgg/SuperRDP) 的重建工程，2026-08 维护版。

> 项目地址：https://github.com/sky12378/SuperRDP2-new  
> 问题反馈：https://github.com/sky12378/SuperRDP2-new/issues

- 纯 C/C++ 源码，MinGW-w64 一键构建，无框架依赖
- **仅 x64**（32 位已停止维护）
- 配置库覆盖 Windows XP ~ Windows 11 25H2 Insider
- 内置**自动分析**：扫描本地 `termsrv.dll` 现场生成补丁配置

## 工作原理

1. `RDPWrap.dll` 安装为 `System32\rdpwrap.dll`，通过注册表把 TermService 的 SvcHost DLL 指向它
2. 包装 DLL 加载真正的 `termsrv.dll` 后，按 `rdpwrap.ini` 中对应版本的偏移量在内存中打补丁（绕过 SingleUser / DefPolicy / LocalOnly 限制）
3. `mstsc.exe` 即可正常连接，支持多用户并发会话

**不替换、不修改系统文件**（补丁全部在内存中进行），卸载即还原。

## 快速开始

> **前置**：Windows 7 64 位及以上；`bin/x64/` 目录内文件必须保持在一起。  
> **杀软**：安装器需写注册表 + 释放 DLL 到 System32，可能被拦截，请放行。

### GUI 版（推荐）

管理员身份运行 `SuperRDPGui.exe`（窗口 471×240）。

| 按钮 | 作用 |
|------|------|
| **安装** | 安装 SuperRDP，若开启「自动分析」会先检查版本支持 |
| **卸载** | 还原注册表、删除 DLL，恢复系统原状 |
| **同步最新配置** | 从 3 个备用源下载最新 `rdpwrap.ini`，自动重装使配置生效 |
| **更新** | 下载并覆盖安装最新版（含 ini 同步 + 重装） |
| **启动 / 停止** | 通过 SCM 启动/停止 TermService 服务 |
| **+** | 强制重启 TermService 服务 |
| **自动分析** | 开关（默认开启）。若版本不在配置中，自动扫描 DLL 定位补丁偏移并追加配置段 |
| **开机启动** | 写入 `HKLM\...\Run`，开机静默执行：下载配置 → 分析 → 重装 |

### 控制台版

管理员身份运行 `SuperRDP.exe`，按提示选择 1/2/3，或直接 `SuperRDP.exe update`（卸载旧版并重装）。

### 验证

`Win+R` → `mstsc.exe` → 计算机填 `127.0.0.1` → 连接。能弹出登录界面即为成功。

## 自动分析（AutoSupport）

开启「自动分析」后，若 `termsrv.dll` 版本不在配置中：

1. 用跨版本稳定的字节签名扫描本地 `termsrv.dll`
2. 自动定位 DefPolicy / SingleUser / LocalOnly / SLInit 四个关键偏移
3. 在 `rdpwrap.ini` 末尾追加该版本的补丁章节（SLInit 数据克隆自同系列最近版本）
4. 随后正常安装

> 局限：SLInit 数据偏移在同系列内可能漂移，克隆属启发式。极新版本若分析后仍无法连接，请通过 issue 上传 `termsrv.dll`。

## 从源码构建

```sh
./build.sh        # 产出 bin/x64/
```

要求：PATH 中有 MinGW-w64 工具链（`x86_64-w64-mingw32-g++`、`windres`）。实测 GCC 8.1 ~ 13 均可构建。

## 目录结构

```
SuperRDP2-new/
├── .github/workflows/      # GitHub Actions（每日同步上游 rdpwrap.ini）
├── build.sh                # 一键构建
├── src/
│   ├── Installer/          # 控制台安装器（SuperRDP.exe）
│   ├── Gui/                # GUI 前端（SuperRDPGui.exe）
│   └── RDPWrap/            # 服务包装 DLL（RDPWrap.dll）
├── bin/x64/                # 成品（exe + dll + ini）
├── README.md / README.en.md
└── LICENSE
```

## 常见问题

**Q: 安装后 mstsc 连不上？**  
检查：TermService 是否运行（点 **+** 重启）→ `rdpwrap.ini` 是否包含你的版本 → 开「自动分析」重装。

**Q: 防火墙需要开端口吗？**  
本机回环不需要；局域网/远程连接需放行 TCP 3389。

**Q: 开机启动会弹 UAC 吗？**  
会。GUI 需要管理员权限，manifest 为 `requireAdministrator`。

## 致谢

- [anhkgg/SuperRDP](https://github.com/anhkgg/SuperRDP) — C/C++ 重写的安装器与补丁引擎
- [stascorp/rdpwrap](https://github.com/stascorp/rdpwrap) — 原始 RDP Wrapper Library
- [sebaxakerhtc / asmtron](https://github.com/asmtron/rdpwrap) — `rdpwrap.ini` 配置库贡献者

## 许可与免责

Apache License 2.0（见 [LICENSE](LICENSE)）。

**免责声明**：本工具仅供在自己拥有或被明确授权的系统上使用，作者不对任何滥用行为承担责任。
