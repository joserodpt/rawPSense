# rawPSense

A minimal PredatorSense replacement for the Acer Predator Helios 300 (PH315-53), built with Qt5.

- Turbo on/off, **without PredatorSense's battery check**
- Live CPU / GPU / system temperatures and CPU / GPU fan RPM
- Manual fan control per fan: Auto, Max, or Custom percentage (Windows only)
- Lives in the system tray: closing or minimising hides the window; the tray icon shows temps/RPM
  on hover and has a Turbo toggle and Quit in its right-click menu
- Optional start at sign-in (Settings), minimised to the tray. On Windows this is a Task Scheduler
  task with highest privileges, so it starts as administrator without a UAC prompt

> Running turbo with the battery removed: under heavy load the laptop can draw more than the
> charger supplies (the battery normally covers those peaks), which can cause sudden shutdowns.

## How it works

| | Windows | Linux |
|---|---|---|
| Interface | `root\WMI` → `AcerGamingFunction` (same as PredatorSense) | kernel `acer-wmi` driver (sysfs) |
| Turbo | LED + fans max + GPU OC calls | `/sys/firmware/acpi/platform_profile` = `performance` |
| Sensors | `GetGamingSysInfo` | hwmon device named `acer` (+ `coretemp` fallback) |
| Fan control | `SetGamingFanBehavior` / `SetGamingFanSpeed` | not exposed by the driver |
| Privileges | runs as administrator (UAC prompt) | root needed to switch turbo |

### Windows WMI values (verified on a PH315-53)

| Action | Method | Input |
|---|---|---|
| Turbo LED on / off | `SetGamingLED` | `0x10001` / `0x1` |
| Both fans max / auto | `SetGamingFanBehavior` | `0x820009` / `0x410009` |
| GPU OC turbo / normal | `SetGamingMiscSetting` | `0x205`+`0x207` / `0x5`+`0x7` |
| Fan custom mode | `SetGamingFanBehavior` | CPU `0x30001`, GPU `0xC00008` |
| Fan custom speed | `SetGamingFanSpeed` | `(percent << 8) \| idx`, CPU idx `1`, GPU idx `4` |
| Sensor reading | `GetGamingSysInfo` | `0x1 \| (id << 8)`: CPU temp `1`, CPU fan `2`, system temp `3`, GPU fan `6`, GPU temp `0xA` |

Sensor result: bits 0-7 status (0 = ok), temperature in bits 8-15, RPM in bits 8-23.

## Build on Windows (MSYS2)

1. Install [MSYS2](https://www.msys2.org/), open the **UCRT64** shell and run:
   ```sh
   pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt5-base,qt5-tools}
   ```
2. Double-click `build.cmd`, or run it from cmd / PowerShell:
   ```bat
   build.cmd
   build.cmd -Clean     (full rebuild)
   ```
   If MSYS2 isn't in `C:\msys64`, set `MSYS2_ROOT` first.
3. Run `dist\rawPSense\rawPSense.exe` (it asks for administrator rights). `dist\rawPSense` contains
   the exe with every Qt plugin and DLL it needs, so it can be copied anywhere.

## Build on Linux

```sh
sudo apt install build-essential cmake qtbase5-dev    # Debian/Ubuntu
# sudo dnf install gcc-c++ cmake qt5-qtbase-devel     # Fedora
# sudo pacman -S base-devel cmake qt5-base            # Arch
./build.sh                 # add --clean for a full rebuild
sudo ./build/rawPSense
```

`./build.sh --install` also installs the binary, icon and menu entry under `/usr/local`.

Notes:
- Fan/temperature sensors need a recent kernel whose `acer-wmi` exposes hwmon for this model.
  If they show `--`, try `sudo modprobe -r acer_wmi && sudo modprobe acer_wmi predator_v4=1`.
- On Linux the physical Turbo key is handled by `acer-wmi` itself, with no battery check.
