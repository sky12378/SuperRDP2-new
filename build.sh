#!/bin/sh
# SuperRDP2-new build script (MinGW-w64, no Visual Studio required)
# 仅构建 x64（64 位）
# Usage:
#   ./build.sh        -> bin/x64/  (default)
#   ./build.sh x64    -> bin/x64/
set -e

ARCH="${1:-x64}"
if [ "$ARCH" != "x64" ]; then
  echo "ERROR: 32-bit (x86) builds discontinued since 2026-08. Only x64 is supported." >&2
  echo "       Run: ./build.sh   (or ./build.sh x64)" >&2
  exit 1
fi
P=x86_64-w64-mingw32
ROOT="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$ROOT/bin/$ARCH"
# some toolchains ship plain `windres`, others the triple-prefixed one
WINDRES="$P-windres"
command -v "$WINDRES" >/dev/null 2>&1 || WINDRES=windres

# ---------- RDPWrap.dll (service wrapper) ----------
cd "$ROOT/src/RDPWrap"
$P-g++ -shared -O2 -fpermissive -include stdafx.h \
  -DUNICODE -D_UNICODE -DNDEBUG -DWIN32 -D_WINDOWS -D_USRDLL -DRDPWRAP_EXPORTS \
  -fpack-struct=1 -static -Wl,--subsystem,windows \
  -o ../../bin/$ARCH/RDPWrap.dll \
  dllmain.cpp IniFile.cpp RDPWrap.cpp stdafx.cpp util.cpp Export.def -lshlwapi

# ---------- SuperRDP.exe (console installer) ----------
cd "$ROOT/src/Installer"
$WINDRES SuperRDP.rc -O coff -o SuperRDP_res.o
$WINDRES manifest.rc   -O coff -o manifest.o
$P-g++ -O2 -fpermissive -municode -DSTRSAFE_NO_DEPRECATE \
  -DUNICODE -D_UNICODE -DNDEBUG -D_CONSOLE -DWIN32 -static \
  IniFile.cpp pch.cpp SuperRDP.cpp Registry.cpp SuperRDP_res.o manifest.o \
  -lshlwapi -luser32 -o ../../bin/$ARCH/SuperRDP.exe

# ---------- SuperRDPGui.exe (GUI front-end) ----------
cd "$ROOT/src/Gui"
$WINDRES SuperRDPGui.rc -O coff -o guires.o
$WINDRES guimanifest.rc -O coff -o guimanifest.o
$P-g++ -O2 -municode -mwindows -static -DUNICODE -D_UNICODE \
  SuperRDPGui.cpp guires.o guimanifest.o \
  -lshlwapi -lurlmon -lgdi32 -lcomctl32 -lversion -lws2_32 -lwtsapi32 -lshell32 -o ../../bin/$ARCH/SuperRDPGui.exe

rm -f "$ROOT/src/Installer"/*.o "$ROOT/src/Gui"/*.o
echo "OK -> bin/$ARCH: SuperRDP.exe, RDPWrap.dll, SuperRDPGui.exe"
