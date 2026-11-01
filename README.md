<p align="center">
  <img src="src/gui/icons/pointeraccel.svg" width="112" height="112" alt="PointerAccel">
</p>

<h1 align="center">PointerAccel</h1>

<p align="center">Windows 11 pointer acceleration for KDE Plasma Wayland.</p>

<p align="center"><strong>English</strong> · <a href="README.zh-CN.md">简体中文</a></p>

<p align="center">
  <a href="#installation">Installation</a> ·
  <a href="#usage">Usage</a> ·
  <a href="#building-and-testing">Build</a> ·
  <a href="#documentation">Documentation</a> ·
  <a href="#license">License</a>
</p>

PointerAccel brings the Windows 11 pointer engine from [windows-pointer-linux](https://github.com/junaga/windows-pointer-linux) to **Fedora x86-64 / KDE Plasma 6.7–6.8 / Wayland**, with a native KWin module, a Qt Widgets control panel, and a command-line interface.

## Features

- **Adjustable pointer acceleration** — 20 speed levels and Enhance Pointer Precision (EPP). Defaults to 10/20 with EPP enabled.
- **Per-device control** — apply acceleration to physical relative mice and exclude individual devices. Touchpads, tablets, absolute pointing devices, and virtual devices pass through.
- **Display scaling support** — adapt effective DPI to the display under the pointer, without entering mouse DPI or polling rate.
- **Preserved raw input** — keep unaccelerated/raw motion unchanged for games and other applications.
- **GUI and CLI** — configure settings, manage devices, and inspect diagnostics. Processing continues after the control panel closes.

## Requirements

| Component | Requirement |
| --- | --- |
| Platform | Fedora, x86-64 |
| Session | KDE Plasma / Wayland |
| KWin | 6.7 and 6.8 series; runtime and development packages must match |
| Tested environment | Fedora 44, KWin 6.8.2, Qt 6.11.2 |

The module uses KWin internal interfaces and is tied to a specific KWin build. KWin 6.9 and newer are not supported. KWin package updates, including rebuilds of the same version, may require rebuilding the module. KWin 6.7 support retains its earlier 6.7.5 validation; the current adaptation is tested on 6.8.2.

## Installation

### 1. Install dependencies

The following packages are required for the tested Fedora 44 / KWin 6.8.2 environment:

```bash
sudo dnf install \
  gcc-c++ cmake ninja-build extra-cmake-modules pkgconf-pkg-config \
  kwin-devel \
  qt6-qtbase-devel \
  kf6-kcoreaddons-devel kf6-kconfig-devel kf6-kwindowsystem-devel \
  wayland-devel wayland-protocols-devel libinput-devel systemd-devel \
  libxkbcommon-devel libdrm-devel libepoxy-devel vulkan-loader-devel \
  libX11-devel libXi-devel gtest-devel
```

The versions and release numbers of `kwin`, `kwin-libs`, and `kwin-devel` must match. Tests also require Python 3, `kscreen-doctor`, `dbus-run-session`, and `gdbus`.

### 2. Build and install

Run from the project directory as your regular user:

```bash
./install.sh
```

The script checks the environment, builds the project, and runs the tests before installing. It requests sudo only to install the system module and compatibility records. **Do not run `sudo ./install.sh`.**

### 3. Log in again

Save your work, log out of Plasma, and log back in. Then open the control panel:

```bash
pointeraccel gui
```

If `~/.local/bin` is not in your PATH, use `~/.local/bin/pointeraccel gui`.

## Usage

### Control panel

Run `pointeraccel gui` to adjust acceleration, manage devices, and view diagnostics. Changes remain in a draft until you click **应用 (Apply)**. Closing the window leaves pointer processing running.

### Command line

```bash
# Check status and installation health
pointeraccel status
pointeraccel doctor

# Enable, disable, and configure acceleration
pointeraccel enable
pointeraccel disable
pointeraccel config set pointer-speed 12/20
pointeraccel config set enhance-pointer-precision off

# List devices and inspect diagnostics
pointeraccel devices
pointeraccel diagnostics --json
```

Settings are saved to `~/.config/pointeraccel/config.json`. Use `enable --temporary` or `disable --temporary` for a temporary override. An `idle` status means the module is enabled and waiting for mouse input.

## Updates and repair

After a KWin update, check compatibility and repair the installation if needed:

```bash
pointeraccel status --json
pointeraccel repair
```

Repair rebuilds and tests the module using the source snapshot saved during installation. Log out and back in after it finishes. To install changes from this repository, run `./install.sh` again.

For the first upgrade from the 6.7-only plugin to KWin 6.8, run `./install.sh` from this updated repository. The old installed source snapshot cannot provide the new adaptation. Installation also refreshes the snapshot used by future repairs.

If an installation is interrupted, run `./install.sh --recover` from the source directory, then rerun `./install.sh`.

## Uninstallation

```bash
./uninstall.sh
```

If the source directory is no longer available:

```bash
~/.local/share/pointeraccel/uninstall.sh
```

Uninstallation restores the previous KWin enable setting and application configuration. Log out and back in to fully unload the module.

## Building and testing

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure

# Launch the control panel from the build directory
./build/pointeraccel gui
```

## Documentation

The detailed guides below are in Chinese:

- [User guide](docs/usage.md) — GUI, CLI, device management, and diagnostics.
- [Installation and troubleshooting](docs/installation.md) — compatibility, upgrades, recovery, and uninstallation.
- [Architecture](docs/architecture.md) — module responsibilities and input processing.
- [Algorithm origin](docs/algorithm-origin.md) — upstream version, source commit, and provenance.
- [Validation](docs/validation.md) — test results and hardware validation boundaries.
- [Code style](docs/formatting.md) — formatting conventions and checks.

## License

Original project code is licensed under the [MIT License](LICENSE). The integrated `windows-pointer-linux` engine and retained upstream tests are licensed under **BSD-3-Clause**.

<a id="third-party-license"></a>

### Third-party license: windows-pointer-linux

<!-- BEGIN WINDOWS-POINTER-LINUX LICENSE -->

```text
BSD 3-Clause License

Copyright (c) 2026, windows-pointer-linux contributors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

```

<!-- END WINDOWS-POINTER-LINUX LICENSE -->
