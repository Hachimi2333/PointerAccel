# PointerAccel

在 KDE Plasma **6.7 / Wayland / Fedora x86-64** 中复用 `windows-pointer-linux` 的 Windows 11 桌面指针引擎。默认速度为 **10/20**，开启“提高指针精确度”（EPP）。

该程序作用于物理相对鼠标；触摸板、平板、绝对坐标和虚拟设备透传。多只鼠标共享桌面指针状态，按光标所在屏幕的缩放选择有效 DPI。游戏的 unaccelerated/raw 位移保持原值。

## 安装

请在项目目录运行，**不要使用 `sudo ./install.sh`**：

```bash
./install.sh
```

脚本先检查 KWin 运行包与开发包、构建、运行测试，再安装。只有安装一个独立 KWin 模块及其兼容记录时调用 `sudo`，在终端输入密码即可。它不会安装依赖包，不会重启桌面。安装成功后保存工作，手动注销并重新登录 Plasma。

登录后，独立的用户服务先校验安装和 KWin 文件，再请求 KWin 加载模块。KWin 的 `pointeraccel_kwinEnabled` 直接自启键保持关闭，这是正常配置。检查失败时使用原生输入，并通过桌面通知和 `doctor` 报告原因。检查服务不链接 `libkwin`，也不会在后台编译或提权。

Fedora 44 / KWin 6.7.5 的构建依赖：

```bash
sudo dnf install \
  gcc-c++ cmake ninja-build extra-cmake-modules pkgconf-pkg-config \
  kwin-devel-6.7.5-1.fc44 \
  qt6-qtbase-devel qt6-qtdeclarative-devel \
  kf6-kcoreaddons-devel kf6-kconfig-devel kf6-kwindowsystem-devel \
  wayland-devel wayland-protocols-devel libinput-devel systemd-devel \
  libxkbcommon-devel libdrm-devel libepoxy-devel vulkan-loader-devel \
  libX11-devel libXi-devel gtest-devel
```

测试还使用 Plasma 自带的 `kscreen-doctor`、`dbus-run-session`、`gdbus` 及系统 Python 3。第一次运行后，CLI 位于 `~/.local/bin/pointeraccel`；若终端尚未把此目录加入 PATH，请使用完整路径。

## 使用

```bash
pointeraccel status
pointeraccel doctor
pointeraccel repair
pointeraccel info --json
pointeraccel disable
pointeraccel enable
pointeraccel disable --temporary
pointeraccel config get
pointeraccel config set pointer-speed 12/20
pointeraccel config set enhance-pointer-precision off
pointeraccel devices
pointeraccel devices exclude mouse-0123456789abcdef01234567
pointeraccel devices include mouse-0123456789abcdef01234567
pointeraccel reset-state
pointeraccel diagnostics --json
```

`mouse-…` 必须替换为 `devices` 返回的真实 ID。ID 根据设备型号、名称、序列号或物理路径生成，不使用会在重启后变化的 event 编号。没有序列号的鼠标更换 USB 接口后可能获得新 ID。

启停默认持久保存；`--temporary` 仅改变当前模块的状态，下一次加载恢复保存值。速度、EPP 和排除列表立即应用。配置保存在 `~/.config/pointeraccel/config.json`，直接编辑后可用任一持久 CLI 设置触发重载。无效配置会停用算法，不会静默采用错误值。

状态分别显示保存的启用设置、模块是否加载、实际是否生效及健康状态。`idle` 表示已启用但尚未处理鼠标报告，不是故障。`doctor` 正常返回 0，异常、需要修复或需要重新登录时返回 1；命令行参数错误返回 2。正常禁用和空闲仍返回 0。

短期记录逐报告诊断数据：

```bash
pointeraccel config set trace on
pointeraccel diagnostics --json
pointeraccel config set trace off
```

记录默认关闭，仅存于模块内存，最多 4096 份；关闭后清除记录计数。常规诊断只保留最近一份样本和累计计数。输入处理过程中不写磁盘、不调用同步 D-Bus。

## KWin 升级与修复

模块二进制绑定具体 KWin 构建：版本 IID、库与可执行文件的 SHA-256 都必须匹配。同为 6.7.5 的发行版重打包也可能需要重新编译。升级后运行：

```bash
pointeraccel status --json
pointeraccel repair
```

修复默认使用安装时保存的源码快照，原项目目录移动或删除后仍可使用。它在独立缓存目录构建并运行完整测试，成功后调用 sudo 安装；不自动拉取代码或安装依赖。若需要使用修改后的源码：

```bash
pointeraccel repair --source /path/to/PointerAccel
```

修复成功后手动注销并重新登录。程序不会把新模块热加载到当前 KWin，也不会自动重启桌面。首次使用本次改进时，先在源码目录运行 `./install.sh`，把旧的直接自启安装迁移到检查服务。

| 状态字段/值 | 含义 |
| --- | --- |
| `running_compatible` | 当前 KWin 与已加载模块是否匹配 |
| `startup_compatible` | 磁盘上的 KWin 与安装模块是否匹配 |
| `startup_state: ready` | 当前登录允许检查服务加载模块 |
| `startup_state: pending-login` | 安装/修复已完成或运行版本落后，需要重新登录 |
| `startup_state: repair-required` | 支持的 6.7 系列已变化，需要重建 |
| `startup_state: unsupported` | KWin 不属于已支持的 6.7 系列，需要代码适配 |
| `startup_state: blocked` | 安装文件、兼容清单或事务异常，已阻止加载 |
| `repair_required` / `relogin_required` | 分别表示需要修复或重新登录 |

在线升级时，旧 KWin 与旧模块可以继续正常工作，因此可能同时看到 `active: true` 和 `repair_required: true`。模块运行时核验进程内的 GNU Build ID，配置变更不会仅因磁盘更新而停用旧模块。下次登录若不匹配则不加载模块，鼠标恢复系统原生行为。

KWin 6.8 及以后明确拒绝构建和自动加载；重新编译不能代替接口适配。检查服务监听会话和文件变化，每 30 秒补查一次文件属性，仅变化时重新计算哈希；相同故障和构建组合的桌面通知去重。

安装/修复的构建、测试、更新竞态或部署失败会中止并恢复已有安装。若断电或强制杀进程留下未完成事务，从源码目录执行：

```bash
./install.sh --recover
./install.sh
```

`--recover` 恢复未提交事务的系统模块及用户文件；系统端已经提交且用户清单完整时保留已完成安装。恢复日志与备份保存在 `XDG_STATE_HOME` 下、安装状态目录旁，正常完成后删除。日志或备份已丢失时停止自动恢复并保留现状，需要人工核查，不能强行覆盖。

## 卸载与恢复

项目目录内：

```bash
./uninstall.sh
```

即使源码已被移走，仍可运行安装时保留的独立卸载入口：

```bash
~/.local/share/pointeraccel/uninstall.sh
```

无法进入桌面时，从 TTY 使用：

```bash
~/.local/share/pointeraccel/uninstall.sh --offline
```

正常卸载先恢复原生输入，再移除自有系统模块和用户文件，恢复原 KWin 启用键与安装前已有的程序配置。KWin 配置未被另行编辑时按字节恢复原文件；存在后续修改时只恢复本程序的键。共享依赖包和项目源码保留。

卸载不自动注销。当前 KWin 已加载的模块可能继续驻留内存，重新登录后彻底清除。`--offline` 适用于会话总线不可用的恢复场景，不能保证关闭另一个仍活动的图形会话中的模块；应退出该图形会话。

安装文件被人工修改、清单缺失、系统模块所有者不同或校验值不匹配时，脚本停止并报告具体路径，不强行删除。v1 的系统模块由一个安装用户登记所有权；不会覆盖另一用户登记的模块。

## 安装修改清单

| 位置 | 内容 |
| --- | --- |
| `/usr/lib64/qt6/plugins/kwin/plugins/pointeraccel_kwin.so` | 独立原生模块，root 所有；其他用户默认不启用 |
| `/usr/local/share/pointeraccel/` | 系统模块所有权、兼容清单、校验值及安装事务记录 |
| `~/.local/share/pointeraccel/` | CLI、诊断工具、卸载脚本、源码快照、说明与许可证 |
| `~/.local/bin/pointeraccel` | 指向 CLI 的符号链接 |
| `~/.config/pointeraccel/config.json` | 用户设置 |
| `~/.local/state/pointeraccel/` | 文件清单、配置备份、安装会话与通知去重记录 |
| `~/.config/systemd/user/pointeraccel-session.service` 及其 Plasma target 链接 | 当前用户的兼容检查与延迟加载服务 |
| `~/.config/kwinrc` 的 `[Plugins] pointeraccel_kwinEnabled` | 设为 false，交由检查服务加载；卸载恢复原值 |

用户数据目录遵循 `XDG_DATA_HOME`、`XDG_CONFIG_HOME` 和 `XDG_STATE_HOME`。安装和卸载时应保持这些变量一致。系统目录固定为本项目验证过的 Fedora 布局。

不会改写系统 KWin/libinput 文件，不修改其文件能力，不修改 `kcminputrc`，不创建用户服务覆盖，也不关闭 SELinux 或 Secure Boot。

## 原理与兼容边界

引擎取自 `windows-pointer-linux`，源码以 `src/windows_pointer/engine.cpp` 和 `include/windows_pointer/engine.hpp` 独立保存在本项目中，不依赖原项目文件夹或额外克隆。源码内容保持原样：Q16 定点数、1–20 档速度、EPP 分段和段间状态、带符号余量、零报告规则、多鼠标共享状态。有效显示 DPI 使用原引擎的 `round(96 × scale)`，限定于 96–480。无需再次输入鼠标 DPI 或回报率。来源提交和校验值见 [算法来源](docs/algorithm-origin.md)。

KWin 6.7 会先合并连续的 libinput 报告。模块通过 ELF 导入表接管 libkwin 内的三个调用入口，保留每份报告，再按原顺序交回正常指针处理。它只临时写入导入表的数据页并恢复页权限，不改写机器指令、不创建可写可执行页。停用时恢复导入槽及原 Qt 信号连接。每份重放报告都发送相应输入帧边界，以保留 XWayland raw motion。

Windows 整数输出可能为零，即使原始报告非零。KWin 默认会跳过这种报告的相对消息，因此模块在正常转发过滤器之前补发其 raw 数据；锁屏、拖拽和其他先行过滤器仍先处理事件。桌面位移继续保持算法输出的零值。

模块加载、输入拦截和 KWin 版本检查是不同层次。二进制模块遵守 KWin 的版本 IID，检查服务核对构建时的 KWin 库和可执行文件 SHA-256，模块工厂在创建输入处理对象前核对进程内的 GNU Build ID。缺少导入入口、原函数不匹配、报告校验失败、队列溢出或崩溃恢复启动时，算法不会继续应用。报告批次验证在推进引擎状态前完成，失败批次按原合并事件传递一次。

此适配依赖 KWin 内部接口。KWin 6.7 系列升级后运行 `pointeraccel repair` 进行构建和测试；6.8 及以后明确拒绝构建，需审查适配。修复后重新登录以加载对应的新模块。算法一致性不等于对所有 Windows 版本、游戏或硬件延迟的独立认证。

## 构建与测试

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

测试包含原项目独立参考模型、真实 ELF 完整 RELRO 库的调用拦截与权限恢复、隔离 D-Bus 会话中的嵌套 KWin、Wayland/XWayland raw 客户端，以及临时 HOME/模拟系统目录中的真实安装脚本往返。测试模块含有虚拟设备注入接口；**正式模块不包含该接口**。

手动观察 raw 输入：

```bash
pointeraccel raw-probe wayland
pointeraccel raw-probe x11
```

测试窗口将输出逐报告 JSON；关闭窗口或 Ctrl+C 结束。Windows 引擎本身的正确性沿用上游测试与研究记录。本机真实鼠标、游戏、真实混合缩放多屏及首次登录自启仍需在安装、重新登录后验证，不能用隔离测试代替。

详见 [测试记录](docs/validation.md)。项目代码遵循原有 MIT 许可证；算法引擎及保留的上游测试遵循 [BSD-3-Clause 许可证](LICENSES/windows-pointer-linux.txt)。
