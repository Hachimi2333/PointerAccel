# 构建与测试

测试保持 GTest、QtTest、Python 标准库 unittest 和 CTest，不要求 pytest 或 Python 第三方运行依赖。完整桌面依赖见 README；仅测试引擎需要 C/C++ 编译器、CMake 3.25+、Ninja 和 GTest。

## 三种构建

```bash
# 不查找 Qt/KWin/Python 的纯引擎构建
cmake --preset engine
cmake --build --preset engine
ctest --preset engine

# 全部生产目标和隔离测试
cmake --preset full
cmake --build --preset full
ctest --preset full

# 生产产物，不查找 GTest/QtTest、不编入测试支持
cmake --preset production
cmake --build --preset production
```

原有 `cmake -S . -B build` 命令继续有效。`PA_BUILD_DESKTOP` 默认开启，`BUILD_TESTING` 保持 CTest 的默认行为。安装和修复显式开启桌面目标及全部测试，不按标签缩减测试，测试为空时视为失败。

## 测试职责

| 位置 | 职责 |
| --- | --- |
| `tests/engine` | 上游语义、独立模型、场景和历史连续状态基线；保留原文件 |
| `tests/unit` | 配置仓储、草稿状态、启动策略、适配校验、清单契约、产物依赖和源码快照 |
| `tests/integration` | D-Bus 控制契约、GUI 控制器/控件交互、真实 ELF 导入表与采集 |
| `tests/system` | 嵌套 KWin/raw 客户端、冻结输入对照、安装回滚/迁移、升级保护和修复 |
| `tests/support` | 隔离环境、进程管理、RPC、控制 peer、虚拟设备及 GUI 驱动 |
| `tests/fixtures` | 有来源说明的冻结数据，以及 Python/C++ 共用的清单契约 |
| `tests/benchmarks` | 可选合成采集成本测量，不作为功能测试阈值 |

CTest 标签按运行目的划分：`unit`、`contract`、`integration`、`system`。含 KWin 的逐报告输入测试属于 `integration`；安装/升级事务属于 `system`。目录和标签分别表达职责和运行范围。

```bash
# 开发中按范围运行
ctest --test-dir build-full -L unit --output-on-failure
ctest --test-dir build-full -L contract --output-on-failure
ctest --test-dir build-full -R '^input_equivalence$' --output-on-failure

# 单独重跑一个安装场景
ctest --test-dir build-full -R '^install_modified_file_blocks_uninstall$' --output-on-failure
python3 tests/system/install_test.py build-full InstallationTest.test_modified_file_blocks_uninstall -v

# 并行及 JUnit 报告
ctest --test-dir build-full --parallel 4 --output-on-failure --output-junit parallel.xml
```

Python 测试使用 unittest 断言，`python -O` 不会关闭检查。状态连续的测试保留动作序列，独立的安装保护场景各自创建环境。

## 隔离、超时与产物

测试在启动 Qt 应用或私有 D-Bus 前设置临时 HOME/XDG，使用独有 Wayland socket 和模拟系统目录。测试专用 D-Bus 不配置桌面服务自动激活，因此不会启动真实会话的 portal、通知或其他服务。子进程由进程组管理，正常结束、异常、超时和终止信号均清理子进程。

测试资源与诊断文件分别存放：短路径临时目录用于 Unix socket，避免 108 字节路径上限；日志和截图留在 `<build>/test-artifacts/<suite>/<run-id>/`。失败输出显示对应目录，CTest 另保留 `Testing/Temporary/LastTest.log`。Qt 日志为 `qt.log`，嵌套会话为 `kwin.log`，安装等命令输出为 `commands.log`。

自动测试不会把模块部署到真实系统或加载进当前 KWin。测试环境开关仅对隔离测试构建生效。

## 冻结行为

- `cmake/VerifyFrozen.cmake` 校验当前引擎源码/头文件和冻结数据，不使用最初导入上游时的历史哈希。
- `state_baseline.hpp` 和 `continuous_input.json` 保留来自原版本的期望值，正常测试不重生成。
- `input_equivalence.json` 在生产重构前，由 `e73f4c6` 的运行实现和仅增加观察能力的测试模块生成。场景使用两个 640×480 虚拟输出、1x/2x 缩放、固定报告时间戳；对比每份位移、raw、设备、时间戳、光标位置、诊断中的缩放/DPI 与启停状态。
- 新增输入对照覆盖跨屏、配置变化、热插拔、排除/恢复、非整数及触摸板透传、临时和持久启停、损坏批次及队列溢出。原嵌套会话继续验证 Wayland/XWayland raw 顺序、零输出保留 raw 和 GUI 关闭后的模块寿命。
- 不能靠修改参考模型、基线或校验值消除失败；新增行为基线必须独立记录来源及生成条件。

## 可选性能测量

```bash
cmake --preset full -DPA_BUILD_BENCHMARKS=ON
cmake --build --preset full --target pa_capture_benchmark
./build-full/pa_capture_benchmark
```

该数据仅反映合成 libinput 替身中的采集/取出成本，不代表 KWin、协议客户端或真实硬件的端到端延迟，不设置易受机器负载影响的通过阈值。

## 源码快照验收

完整发布或修改构建布局时，除普通测试外还要生成、解包源码快照，在解包目录进行全新生产构建；涉及修复时继续验证快照构建和隔离安装。快照自带 presets、格式配置、AGENTS.md 和测试，无须 Git 历史或原工作目录。

真实鼠标、按钮/滚轮、真实混合缩放多屏、休眠和游戏链路仍需独立人工验证；嵌套会话不替代这些验证。
