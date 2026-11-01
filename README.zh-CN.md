<p align="center">
  <img src="src/gui/icons/pointeraccel.svg" width="112" height="112" alt="PointerAccel">
</p>

<h1 align="center">PointerAccel</h1>

<p align="center">在 KDE Plasma Wayland 上使用 Windows 11 桌面指针加速。</p>

<p align="center"><a href="README.md">English</a> · <strong>简体中文</strong></p>

<p align="center">
  <a href="#安装">安装</a> ·
  <a href="#使用">使用</a> ·
  <a href="#构建与测试">构建</a> ·
  <a href="#文档">文档</a> ·
  <a href="#许可证">许可证</a>
</p>

PointerAccel 将 [windows-pointer-linux](https://github.com/junaga/windows-pointer-linux) 的 Windows 11 指针引擎带到 **Fedora x86-64 / KDE Plasma 6.7 / Wayland**，提供原生 KWin 模块、Qt Widgets 控制面板和命令行工具。

## 功能

- **可调指针加速**：支持 1–20 档速度和“提高指针精确度”（EPP），默认 10/20 档、EPP 开启。
- **按设备控制**：作用于物理相对鼠标，可排除指定设备；触摸板、平板、绝对坐标和虚拟设备透传。
- **显示缩放适配**：按光标所在屏幕的缩放计算有效 DPI，无需填写鼠标 DPI 或回报率。
- **保留原始输入**：游戏及其他应用使用的 unaccelerated/raw 位移保持原值。
- **图形与命令行配置**：调整参数、管理设备和查看诊断；关闭控制面板后继续处理指针输入。

## 环境要求

| 项目 | 要求 |
| --- | --- |
| 平台 | Fedora，x86-64 |
| 桌面会话 | KDE Plasma / Wayland |
| KWin | 6.7 系列，运行包与开发包须为相同构建 |
| 已验证环境 | Fedora 44、KWin 6.7.5、Qt 6.11.2 |

模块使用 KWin 内部接口，绑定具体的 KWin 构建。KWin 6.8 及以后尚未适配；KWin 软件包更新，包括同版本重新打包，都可能需要重新编译模块。

## 安装

### 1. 安装依赖

以下为已验证的 Fedora 44 / KWin 6.7.5 环境使用的依赖：

```bash
sudo dnf install \
  gcc-c++ cmake ninja-build extra-cmake-modules pkgconf-pkg-config \
  kwin-devel-6.7.5-1.fc44 \
  qt6-qtbase-devel \
  kf6-kcoreaddons-devel kf6-kconfig-devel kf6-kwindowsystem-devel \
  wayland-devel wayland-protocols-devel libinput-devel systemd-devel \
  libxkbcommon-devel libdrm-devel libepoxy-devel vulkan-loader-devel \
  libX11-devel libXi-devel gtest-devel
```

`kwin`、`kwin-libs` 和 `kwin-devel` 的版本与发行号必须一致。测试还需要 Python 3、`kscreen-doctor`、`dbus-run-session` 和 `gdbus`。

### 2. 构建并安装

在项目目录中，以普通用户运行：

```bash
./install.sh
```

脚本先检查环境、构建并运行测试，再执行安装，仅在安装系统模块和兼容记录时请求 sudo 权限。**不要运行 `sudo ./install.sh`。**

### 3. 重新登录

保存工作，注销并重新登录 Plasma，然后打开控制面板：

```bash
pointeraccel gui
```

如果 `~/.local/bin` 尚未加入 PATH，请使用 `~/.local/bin/pointeraccel gui`。

## 使用

### 控制面板

运行 `pointeraccel gui`，调整指针加速、管理设备和查看诊断。更改先保留为草稿，点击“应用”后统一保存并请求模块生效。关闭窗口后继续处理指针输入。

### 命令行

```bash
# 检查状态与安装健康
pointeraccel status
pointeraccel doctor

# 启用、禁用与调整参数
pointeraccel enable
pointeraccel disable
pointeraccel config set pointer-speed 12/20
pointeraccel config set enhance-pointer-precision off

# 查看设备与诊断信息
pointeraccel devices
pointeraccel diagnostics --json
```

配置保存在 `~/.config/pointeraccel/config.json`。使用 `enable --temporary` 或 `disable --temporary` 临时覆盖启停状态。`idle` 表示模块已启用，正在等待鼠标输入。

## 更新与修复

KWin 更新后，检查兼容状态并按需修复：

```bash
pointeraccel status --json
pointeraccel repair
```

修复使用安装时保存的源码快照重新构建并测试模块，完成后注销并重新登录。安装当前项目源码的改动时，再次运行 `./install.sh`。

如果安装意外中断，在源码目录执行 `./install.sh --recover`，恢复完成后再运行 `./install.sh`。

## 卸载

```bash
./uninstall.sh
```

源码目录已移走时：

```bash
~/.local/share/pointeraccel/uninstall.sh
```

卸载恢复原 KWin 启用设置及安装前的程序配置。完成后注销并重新登录，以彻底清除已加载模块。

## 构建与测试

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure

# 从构建目录启动控制面板
./build/pointeraccel gui
```

## 文档

- [使用指南](docs/usage.md)：控制面板、CLI、设备管理和诊断。
- [安装与故障排查](docs/installation.md)：兼容状态、升级修复和卸载恢复。
- [代码结构](docs/architecture.md)：模块职责与输入处理。
- [算法来源](docs/algorithm-origin.md)：上游版本、来源提交与溯源信息。
- [测试记录](docs/validation.md)：测试结果与真实硬件验证边界。
- [代码格式](docs/formatting.md)：排版约定与检查命令。

## 许可证

本项目原创代码采用 [MIT License](LICENSE)。融入的 `windows-pointer-linux` 指针引擎及保留的上游测试采用 **BSD-3-Clause**。

<a id="third-party-license"></a>

### 第三方许可证：windows-pointer-linux

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
