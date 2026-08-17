# SuperRDP2-new

Unlocks the **Remote Desktop (RDP)** feature that is gated behind Windows Home / non-Pro editions — a rebuilt fork of [anhkgg/SuperRDP](https://github.com/anhkgg/SuperRDP), maintained as of 2026-08.

[中文](README.md) | **English**

> Project: https://github.com/sky12378/SuperRDP2-new
> Issues: https://github.com/sky12378/SuperRDP2-new/issues
- Pure C/C++ source (native Win32 API UI, no framework dependency), one-shot MinGW-w64 build
- **x64 only** (32-bit discontinued as of 2026-08)
- Config library covers Windows XP ~ Windows 11 25H2 Insider (`termsrv.dll` up to 10.0.29565.1000)
- Built-in **Auto-Support**: library doesn't have your version? Scan the local `termsrv.dll` and generate the patch config on the fly

## How It Works

1. `RDPWrap.dll` is installed as `System32\rdpwrap.dll`, and the registry redirects the TermService service's SvcHost DLL to it
2. The wrapper DLL loads the real `termsrv.dll`, then patches it in memory using the offsets for your version stored in `rdpwrap.ini` (bypassing SingleUser / DefPolicy / LocalOnly limits and faking SLInit server licensing)
3. `mstsc.exe` connects normally; multiple concurrent sessions are supported

**No system files are replaced or modified** — all patches happen in memory; uninstall restores the original state.

## Quick Start

> **Prerequisites**: Windows 7 64-bit or later; the files inside `bin/x64/` must stay together.
> **Antivirus**: the installer writes to the registry and drops a DLL into System32 — it may be flagged. Please allow it.

### GUI Build (recommended)

Run `SuperRDPGui.exe` as Administrator (window 471×240, non-resizable).

#### Button Reference

| Button | Action |
|--------|--------|
| **Install** | Installs SuperRDP: drops `rdpwrap.dll` into System32, redirects the registry, registers TermService. If **Auto-Support** is on, it first checks whether your `termsrv.dll` version is supported by the config; if not, it generates the config on the fly before installing |
| **Uninstall** | Restores the registry, removes the dropped DLL, returns the system to its original state |
| **Sync latest config** | Downloads the latest `rdpwrap.ini` from 3 fallback sources (GitHub raw, via `gh-proxy.com` proxy + direct fallback), then **auto-reinstalls** so the new config takes effect immediately |
| **Update** | Downloads and overwrites with the latest version (includes ini sync + reinstall) |
| **Start** | Starts the TermService service via SCM |
| **Stop** | Stops the TermService service via SCM |
| **+** | Force-restarts the TermService service (use this after changing config) |
| **Auto-Support** | Toggle (on by default, marked with `✓`). When on, during install/sync/update, if your `termsrv.dll` version is missing from the config library, it scans the DLL to locate 4 critical patch offsets (DefPolicy / SingleUser / LocalOnly / SLInit) and appends a config section. See below |
| **Boot startup** | Toggle (marked with `✓`). When on, writes to `HKLM\...\Run` so that on next login it silently runs: download latest config → auto-analyze → reinstall if needed (no window, no interaction) |
| **share to github** | Opens the project homepage |

#### Left Info Panel (auto-refreshes every 15s)

| Row | Meaning |
|-----|---------|
| System version | Local `termsrv.dll` version |
| termsrv | Latest supported version in the config library |
| Status | Installed / Not installed |
| Auto-Support | Current active RDP session count / 9999 |
| termsrv service | TermService running state + port 3389 listening state |

The bottom status line shows the tail of the operation log (e.g. `server is wrong.` means config mismatch).

### Console Build

Run `SuperRDP.exe` as Administrator and follow the prompt:

```
1: Install SuperRDP      2: Uninstall SuperRDP      3: Force restart Terminal Services
```

Or directly: `SuperRDP.exe update` (uninstalls the old version and reinstalls).

### Verify

`Win+R` → `mstsc.exe` → enter `127.0.0.1` → Connect. If the login screen appears, it works.

## Auto-Support

The `rdpwrap.ini` config library is maintained by the community and often lags behind new Windows releases. With **Auto-Support** on, if your `termsrv.dll` version isn't in the config during install:

1. Scan the local `termsrv.dll` using cross-version-stable byte signatures (with wildcards)
2. Automatically locate the four critical offsets: DefPolicy / SingleUser / LocalOnly / SLInit
3. Append a patch section for your version to the end of `rdpwrap.ini` (SLInit data is cloned from the nearest version in the same family; the original file is backed up as `rdpwrap.ini.autobak` before writing)
4. Proceed with normal install

Verified on `10.0.28000.1761` (a version absent from the config library) — all four offsets matched manual analysis exactly.

> Limitation: SLInit data offsets can drift within a family, so cloning is heuristic. For very new versions where analysis still fails to connect, please upload `C:\Windows\System32\termsrv.dll` via an issue.

## Upgrade

After a major Windows update RDP may break — pick one:

- In the GUI, click **Sync latest config** (easiest)
- Manually copy the new `rdpwrap.ini` to `C:\Windows\System32\`
- Re-run install (uninstall then install, or `SuperRDP.exe update` from console)

## Build from Source

```sh
./build.sh        # produces bin/x64/ (64-bit only)
```

Requires MinGW-w64 toolchain in PATH (`x86_64-w64-mingw32-g++`, `x86_64-w64-mingw32-windres`). Tested with GCC 8.1 through 13.

The source already ships with MSVC→GCC porting patches (relative to the upstream original): UTF-16LE→UTF-8, removed SAL annotations, `extern "C"` exports, zero-length array fix, `scanf_s`→`scanf`, `-fpack-struct=1` (RDPWrap.dll only).

## Directory Layout

```
SuperRDP2-new/
├── build.sh                # one-shot build (MinGW-w64)
├── src/
│   ├── Installer/          # console installer (SuperRDP.exe)
│   ├── Gui/                # GUI front-end (SuperRDPGui.exe)
│   └── RDPWrap/            # service wrapper DLL (RDPWrap.dll)
├── bin/x64/                # build output (exe + dll + ini, distributable)
├── README.md               # Chinese README
├── README.en.md            # English README (this file)
└── LICENSE
```

## FAQ

**Q: mstsc can't connect after install?**
Check in order: is TermService running? (in the GUI click **+** to force-restart) → does `C:\Windows\System32\rdpwrap.ini` exist and contain your version? → if your version isn't in the config, enable **Auto-Support** and reinstall.

**Q: Do I need to open a firewall port?**
Loopback (127.0.0.1) doesn't need it; LAN/remote connections require TCP 3389 to be allowed.

**Q: Does boot-startup trigger UAC?**
Yes. Because the GUI needs admin rights (writing registry + installing service), its manifest is `requireAdministrator`; when launched by the system on login it will prompt UAC.

## Known Limitations

- 64-bit only; the embedded `rfxvmt.dll` resource is x64, and Win8+ systems ship it natively
- Windows updates may replace `termsrv.dll` and break the config — re-sync to fix
- Antivirus false-positives are normal — add to allowlist

## Acknowledgements

- [anhkgg/SuperRDP](https://github.com/anhkgg/SuperRDP) — C/C++ rewrite of the installer and patch engine
- [stascorp/rdpwrap](https://github.com/stascorp/rdpwrap) — the original RDP Wrapper Library
- [sebaxakerhtc / asmtron](https://github.com/asmtron/rdpwrap) and other community maintainers — `rdpwrap.ini` config library contributors

## License & Disclaimer

Apache License 2.0 (SPDX-License-Identifier: Apache-2.0; see [LICENSE](LICENSE)).

**Disclaimer**: This tool is intended for use only on systems you own or are explicitly authorized to use. Users are responsible for complying with local laws. The author is not liable for any misuse.
