# SuperRDP2-new

解锁 Windows Home / 非 Pro 版本被阉割的**远程桌面（RDP）**功能 —— 基于 [anhkgg/SuperRDP](https://github.com/anhkgg/SuperRDP) 的重建工程，2026-08 维护版。

- 纯 C/C++ 源码（Win32 API 原生界面，无框架依赖），MinGW-w64 一键构建
- **仅 x64**（32 位已于 2026-08 停止维护）
- 配置库覆盖 Windows XP ~ Windows 11 25H2 Insider（`termsrv.dll` 最高 10.0.29565.1000）
- 内置**自动分析**：配置库没有你的系统版本？扫描本地 `termsrv.dll` 现场生成补丁配置

## 工作原理

1. `RDPWrap.dll` 安装为 `System32\rdpwrap.dll`，通过注册表把 TermService 服务的 SvcHost DLL 指向它
2. 包装 DLL 加载真正的 `termsrv.dll` 后，按 `rdpwrap.ini` 中对应版本的偏移量在内存中打补丁（绕过 SingleUser / DefPolicy / LocalOnly 限制，并伪造 SLInit 服务器授权）
3. `mstsc.exe` 即可正常连接，支持多用户并发会话

**不替换、不修改系统文件**（补丁全部在内存中进行），卸载即还原。

## 快速开始

> **前置**：Windows 7 64 位及以上；`bin/x64/` 目录内文件必须保持在一起。
> **杀软**：安装器需写注册表 + 释放 DLL 到 System32，可能被拦截，请放行。

### GUI 版（推荐）

管理员身份运行 `SuperRDPGui.exe`（窗口 471×240，不可缩放）。

#### 按钮说明

| 按钮 | 作用 |
|------|------|
| **安装** | 安装 SuperRDP：释放 `rdpwrap.dll` 到 System32、改注册表指向、注册 TermService。若已开启「自动分析」，安装前会先检查当前 `termsrv.dll` 版本是否被配置支持，不支持则现场生成配置 |
| **卸载** | 还原注册表、删除释放的 DLL，恢复系统原状 |
| **同步最新配置** | 从 3 个备用源（GitHub raw，走 `gh-proxy.com` 代理 + 直连回退）下载最新 `rdpwrap.ini`，校验后**自动重装**使新配置立即生效 |
| **更新** | 下载并覆盖安装最新版（含 ini 同步 + 重装） |
| **启动** | 通过 SCM 启动 TermService 服务 |
| **停止** | 通过 SCM 停止 TermService 服务 |
| **+** | 强制重启 TermService 服务（改配置后点此生效） |
| **自动分析** | 开关（默认开启，带 `✓`）。开启后，安装/同步/更新时若发现当前 `termsrv.dll` 版本不在配置库中，自动扫描 DLL 定位 4 个关键补丁偏移（DefPolicy / SingleUser / LocalOnly / SLInit）并追加配置段。详见下文 |
| **开机启动** | 开关（带 `✓`）。开启后写入 `HKLM\...\Run`，开机登录后静默执行：下载最新配置 → 自动分析 → 需要时自动重装（无窗口、无交互） |
| **关于** | 弹出版本信息 |
| **送杯咖啡** | 打开赞助链接 |
| **share to github** | 打开项目主页 |

#### 左侧信息面板（每 15 秒自动刷新）

| 行 | 含义 |
|----|------|
| 系统版本 | 本机 `termsrv.dll` 版本号 |
| termsrv | 配置库中最新支持的版本号 |
| 状态 | 已安装 / 未安装 |
| 自动分析 | 当前活动 RDP 会话数 / 9999 |
| termsrv服务 | TermService 运行状态 + 3389 端口监听状态 |

底部状态行显示操作日志末行（如 `server is wrong.` 表示配置不匹配）。

### 控制台版

管理员身份运行 `SuperRDP.exe`，按提示选择：

```
1: Install SuperRDP      2: Uninstall SuperRDP      3: Force restart Terminal Services
```

或直接 `SuperRDP.exe update`（卸载旧版并重装）。

### 验证

`Win+R` → `mstsc.exe` → 计算机填 `127.0.0.1` → 连接。能弹出登录界面即为成功。

## 自动分析（AutoSupport）

`rdpwrap.ini` 靠社区人工维护，新 Windows 版本常有滞后。开启「自动分析」后，安装时若发现你的 `termsrv.dll` 版本不在配置中：

1. 用跨版本稳定的字节签名（带通配符）扫描本地 `termsrv.dll`
2. 自动定位 DefPolicy / SingleUser / LocalOnly / SLInit 四个关键偏移
3. 在 `rdpwrap.ini` 末尾追加该版本的补丁章节（SLInit 数据克隆自同系列最近版本，写入前自动备份为 `rdpwrap.ini.autobak`）
4. 随后正常安装

已在 `10.0.28000.1761`（配置库中不存在的版本）真实验证，四项偏移与人工分析完全一致。

> 局限：SLInit 数据偏移在同系列内可能漂移，克隆属启发式。极新版本若分析后仍无法连接，请通过 issue 上传 `C:\Windows\System32\termsrv.dll`。

## 升级

Windows 大版本更新后 RDP 可能失效，任选其一：

- GUI 点 **同步最新配置**（最省事）
- 手动把新版 `rdpwrap.ini` 拷到 `C:\Windows\System32\`
- 重跑安装（先卸载再安装，或控制台 `SuperRDP.exe update`）

## 从源码构建

```sh
./build.sh        # 产出 bin/x64/（仅 64 位）
```

要求：PATH 中有 MinGW-w64 工具链（`x86_64-w64-mingw32-g++`、`x86_64-w64-mingw32-windres`）。实测 GCC 8.1 ~ 13 均可构建。

源码已内置 MSVC→GCC 移植补丁（相对上游原版）：UTF-16LE→UTF-8、移除 SAL 注解、`extern "C"` 导出、零长数组修正、`scanf_s`→`scanf`、`-fpack-struct=1`（仅 RDPWrap.dll）。

## 目录结构

```
SuperRDP2-new/
├── build.sh                # 一键构建（MinGW-w64）
├── src/
│   ├── Installer/          # 控制台安装器（SuperRDP.exe）
│   ├── Gui/                # GUI 前端（SuperRDPGui.exe）
│   └── RDPWrap/            # 服务包装 DLL（RDPWrap.dll）
├── bin/x64/                # 成品（exe + dll + ini，可直接分发）
├── README.md
└── LICENSE
```

## 常见问题

**Q: 安装后 mstsc 连不上？**
依次检查：TermService 是否运行（GUI 点 **+** 强制重启）→ `C:\Windows\System32\rdpwrap.ini` 是否存在且包含你的版本 → 版本不在配置里就开「自动分析」重装。

**Q: 防火墙需要开端口吗？**
本机回环（127.0.0.1）不需要；局域网/远程连接需放行 TCP 3389。

**Q: 开机启动会弹 UAC 吗？**
会。因 GUI 需要管理员权限（写注册表 + 安装服务），manifest 为 `requireAdministrator`，每次开机登录由系统启动时会弹 UAC 确认。

## 已知限制

- 仅 64 位；内嵌的 `rfxvmt.dll` 资源为 x64，Win8+ 系统自带
- Windows 更新可能替换 `termsrv.dll` 导致配置失效，重新同步即可
- 杀软误报属正常现象，白名单处理

## 致谢

- [anhkgg/SuperRDP](https://github.com/anhkgg/SuperRDP) — C/C++ 重写的安装器与补丁引擎（汉客儿）
- [stascorp/rdpwrap](https://github.com/stascorp/rdpwrap) — 原始 RDP Wrapper Library
- [sebaxakerhtc / asmtron](https://github.com/asmtron/rdpwrap) 等社区维护者 — `rdpwrap.ini` 配置库贡献者

## 许可与免责

Apache License 2.0（SPDX-License-Identifier: Apache-2.0；见 [LICENSE](LICENSE)）。

**免责声明**：本工具仅供在自己拥有或被明确授权的系统上使用，使用者需自行遵守当地法律法规，作者不对任何滥用行为承担责任。
