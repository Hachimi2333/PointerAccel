# 使用指南

[返回项目首页](../README.zh-CN.md) · [安装与故障排查](installation.md)

## 图形控制面板

```bash
# 从构建目录启动
./build/pointeraccel gui

# 安装后启动
pointeraccel gui
```

面板使用 Qt Widgets，左侧导航切换“设置”“设备”“诊断”，顶部显示运行摘要，底部固定提供撤销与应用操作。控件、字体与颜色跟随系统主题，正文可独立滚动，支持键盘操作和显示缩放。它是独立的查看和配置工具；不集成进 Plasma 设置，不安装菜单项、托盘或 GUI 自启动。关闭窗口仅退出 GUI，算法与会话检查服务继续运行，CLI 的其他命令也不依赖图形环境。

总开关、1–20 档速度、EPP、设备排除和 trace 共用一份草稿，切换页面不会丢失。点击底部“应用”后一次保存全部修改，再请求模块应用；这会清除 CLI 的临时启停覆盖。面板分别显示保存设置、实际生效和健康状态。模块不可用或应用失败时，配置仍会保留，并明确显示“已保存，尚未应用”。关闭时若有草稿，可以应用、放弃或取消；应用失败或未能确认生效时保留窗口以便查看原因。

CLI 或其他窗口更改配置后，面板会同步显示；已有草稿时保留草稿并提示冲突，点击“撤销修改”重新加载后再编辑。配置损坏时只允许查看和诊断，面板显示具体路径与错误，不会用默认值覆盖原文件。未连接的已排除设备也可取消排除，不支持的设备始终透传。

诊断页展示兼容状态、处理计数和最近输入样本，可以重置算法状态、导出包含已保存配置和现有 trace 的 JSON 快照。trace 仍只存在模块内存中，关闭 GUI **不会自动关闭 trace**；需要取消勾选并点击“应用”。修复通过页面提供的可复制命令在终端执行。

窗口显示时每 2 秒刷新运行状态，仅在相应页面查询设备或诊断；最小化后暂停轮询。完整安装与兼容检查异步执行，并按文件变化更新，不阻塞界面。

短期记录逐报告诊断数据：

```bash
pointeraccel config set trace on
pointeraccel diagnostics --json
pointeraccel config set trace off
```

记录默认关闭，仅存于模块内存，最多 4096 份；关闭后清除记录计数。常规诊断只保留最近一份样本和累计计数。输入处理过程中不写磁盘、不调用同步 D-Bus。

## 命令行

```bash
pointeraccel gui
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

## 原始输入探针

```bash
pointeraccel raw-probe wayland
pointeraccel raw-probe x11
```

测试窗口输出逐报告 JSON，关闭窗口或按 Ctrl+C 结束。原始输入的隔离测试与真实硬件验证边界见[测试记录](validation.md)。
