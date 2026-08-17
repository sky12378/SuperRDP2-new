# SuperRDP2-new

[中文](README.md) | **English**

Unlocks the **Remote Desktop (RDP)** feature that is gated behind Windows Home / non-Pro editions — a rebuilt fork of [anhkgg/SuperRDP](https://github.com/anhkgg/SuperRDP), maintained as of 2026-08.

> Project: https://github.com/sky12378/SuperRDP2-new  
> Issues: https://github.com/sky12378/SuperRDP2-new/issues

- Pure C/C++ source, one-shot MinGW-w64 build, no framework dependency
- **x64 only** (32-bit discontinued)
- Config library covers Windows XP ~ Windows 11 25H2 Insider
- Built-in **Auto-Support**: scan local `termsrv.dll` and generate patch config on the fly

## How It Works

1. `RDPWrap.dll` is installed as `System32\rdpwrap.dll`, and the registry redirects TermService's SvcHost DLL to it
2. The wrapper DLL loads the real `termsrv.dll`, then patches it in memory using offsets from `rdpwrap.ini` (bypassing SingleUser / DefPolicy / LocalOnly limits)
3. `mstsc.exe` connects normally; multiple concurrent sessions are supported

**No system files are replaced or modified** — all patches happen in memory; uninstall restores the original state.

## Quick Start

> **Prerequisites**: Windows 7 64-bit or later; files inside `bin/x64/` must stay together.  
> **Antivirus**: the installer writes to registry and drops a DLL into System32 — it may be flagged.

### GUI Build (recommended)

Run `SuperRDPGui.exe` as Administrator (window 471×240).

| Button | Action |
|--------|--------|
| **Install** | Installs SuperRDP; if **Auto-Support** is on, checks version support first |
| **Uninstall** | Restores registry, removes DLL, returns system to original state |
| **Sync latest config** | Downloads latest `rdpwrap.ini` from 3 fallback sources, auto-reinstalls |
| **Update** | Downloads and overwrites with latest version (ini sync + reinstall) |
| **Start / Stop** | Starts/stops TermService via SCM |
| **+** | Force-restarts TermService |
| **Auto-Support** | Toggle (on by default). If version missing from config, scans DLL and appends patch section |
| **Boot startup** | Writes to `HKLM\...\Run` for silent boot-time: download → analyze → reinstall |

### Console Build

Run `SuperRDP.exe` as Administrator, choose 1/2/3, or directly `SuperRDP.exe update` (uninstall old + reinstall).

### Verify

`Win+R` → `mstsc.exe` → enter `127.0.0.1` → Connect. If login screen appears, it works.

## Auto-Support

With **Auto-Support** on, if your `termsrv.dll` version isn't in the config:

1. Scan local `termsrv.dll` using cross-version-stable byte signatures
2. Automatically locate four critical offsets: DefPolicy / SingleUser / LocalOnly / SLInit
3. Append a patch section for your version (SLInit data cloned from nearest version in same family)
4. Proceed with normal install

> Limitation: SLInit data offsets can drift within a family, so cloning is heuristic. For very new versions where analysis still fails, please upload `termsrv.dll` via an issue.

## Build from Source

```sh
./build.sh        # produces bin/x64/
```

Requires MinGW-w64 toolchain in PATH (`x86_64-w64-mingw32-g++`, `windres`). Tested with GCC 8.1 through 13.

## Directory Layout

```
SuperRDP2-new/
├── .github/workflows/      # GitHub Actions (daily upstream rdpwrap.ini sync)
├── build.sh                # one-shot build
├── src/
│   ├── Installer/          # console installer (SuperRDP.exe)
│   ├── Gui/                # GUI front-end (SuperRDPGui.exe)
│   └── RDPWrap/            # service wrapper DLL (RDPWrap.dll)
├── bin/x64/                # build output (exe + dll + ini)
├── README.md / README.en.md
└── LICENSE
```

## FAQ

**Q: mstsc can't connect after install?**  
Check: is TermService running? (click **+** to restart) → does `rdpwrap.ini` contain your version? → enable **Auto-Support** and reinstall.

**Q: Do I need to open a firewall port?**  
Loopback (127.0.0.1) doesn't need it; LAN/remote connections require TCP 3389.

**Q: Does boot-startup trigger UAC?**  
Yes. GUI needs admin rights, manifest is `requireAdministrator`.

## Acknowledgements

- [anhkgg/SuperRDP](https://github.com/anhkgg/SuperRDP) — C/C++ rewrite of installer and patch engine
- [stascorp/rdpwrap](https://github.com/stascorp/rdpwrap) — original RDP Wrapper Library
- [sebaxakerhtc / asmtron](https://github.com/asmtron/rdpwrap) — `rdpwrap.ini` config library contributors

## License & Disclaimer

Apache License 2.0 (see [LICENSE](LICENSE)).

**Disclaimer**: This tool is intended for use only on systems you own or are explicitly authorized to use. The author is not liable for any misuse.
