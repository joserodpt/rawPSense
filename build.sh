#!/usr/bin/env bash
# Builds rawPSense on Linux.
#
#   ./build.sh             incremental build into build/
#   ./build.sh --clean     wipe build/ first
#   ./build.sh --install   also install the binary, icon and menu entry under /usr/local (uses sudo)
set -euo pipefail
cd "$(dirname "$0")"

clean=0
install=0
for arg in "$@"; do
    case "$arg" in
        --clean) clean=1 ;;
        --install) install=1 ;;
        *) echo "usage: $0 [--clean] [--install]" >&2; exit 1 ;;
    esac
done

if ! command -v cmake >/dev/null || ! command -v c++ >/dev/null; then
    echo "cmake and a C++ compiler are required. Install them with one of:" >&2
    echo "  sudo apt install build-essential cmake qtbase5-dev    # Debian/Ubuntu" >&2
    echo "  sudo dnf install gcc-c++ cmake qt5-qtbase-devel     # Fedora" >&2
    echo "  sudo pacman -S base-devel cmake qt5-base            # Arch" >&2
    exit 1
fi

if [ "$clean" = 1 ]; then
    rm -rf build
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

if [ "$install" = 1 ]; then
    sudo cmake --install build
    echo "Installed. Start rawPSense from the app menu, or run: sudo rawPSense"
else
    echo "Built build/rawPSense. Run it with: sudo ./build/rawPSense"
fi
