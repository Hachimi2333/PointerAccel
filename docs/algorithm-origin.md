# 算法来源与许可证

PointerAccel 融合了 [windows-pointer-linux](https://github.com/junaga/windows-pointer-linux) 的最小算法引擎和回归测试，作为本程序内部实现参与构建。无需克隆原项目，也不依赖原项目文件夹。

- 上游版本：1.0.0。
- 来源提交：[`0bdd644d420737a7aea506e252fc5e12db34c599`](https://github.com/junaga/windows-pointer-linux/tree/0bdd644d420737a7aea506e252fc5e12db34c599)。
- 许可证：[BSD-3-Clause](../README.md#third-party-license)，原版权声明、许可条件和免责声明完整保留在项目首页。
- 下表记录 2026-10-28 最初导入时的路径与原始 SHA-256，仅用于追溯来源。之后的格式整理及内部融合改变了文件字节和位置，不能将这些历史哈希用作当前源码的校验值。

| 最初导入路径 | 原项目路径 | SHA-256 |
| --- | --- | --- |
| `src/windows_pointer/engine.cpp` | `src/engine.cpp` | `698a09996ae6086e5414da23c84ca2f8c0156ffd99a6a5ba6ec8484c99c6950e` |
| `include/windows_pointer/engine.hpp` | `include/windows_pointer/engine.hpp` | `bfb4a1969ca0dd22af632c9796040cc4ce136ae04d8eff8a9f0f138aac809ff7` |
| `tests/windows_pointer/engine_test.cpp` | `tests/engine_test.cpp` | `f4801b364ade52f89a2a66c7c78f0c2235fb031b3932f06e81ada87538884d3a` |
| `tests/windows_pointer/model_test.cpp` | `tests/model_test.cpp` | `537253e27ee40041b6afb7e300e49b0538345267175376a1455b5304389d299b` |
| `tests/windows_pointer/scenario_test.cpp` | `tests/scenario_test.cpp` | `056ae7ebd93b2c2db8a05b88a35bfa09598a6c1c49cbb2a680a315a623605512` |
| `tests/windows_pointer/reference_model.hpp` | `tests/reference_model.hpp` | `fc6ddf3a5ce7a14b172015835ef6f00a50796c3800ee97cbdb23743dfd03ad54` |
| `LICENSES/windows-pointer-linux.txt` | `LICENSE` | `6e32a6f6623073fc97f5d043c253e9ef566ac534ff438a2780f56abc0c740990` |

原项目的 Hyprland 适配器、实验环境和其他工具未包含在本项目中。PointerAccel 的 KWin 适配、CLI、安装卸载脚本及诊断工具由本项目维护。当前使用 `pa::PointerEngine`、`pa::PointerSettings` 和 `pa::Motion`，源码及私有头文件位于 `src/core/`，上游测试和独立参考模型位于 `tests/engine/`。没有独立的上游构建目标或公共头文件。

2026-11-01 重构只调整引擎名称、命名空间、引用路径和来源注释；剔除这些差异与空白后，引擎实现与重构前提交 `7f67730` 一致。曲线常量、Q16 运算、段历史、余量、零报告和 DPI 规则均保留。独立参考模型不复用生产计算函数。

`tests/engine/state_baseline.hpp` 保存重构前生成的 160 步连续状态结果，包含配置、EPP、DPI 变化和显式重置。`tests/fixtures/continuous_input.json` 则来自提交 `7f67730` 的实际嵌套 KWin 模块，仅将测试注入中的自动重置改为可选，覆盖跨批次输入、多鼠标、缩放、热插拔、排除、启停和错误回退。正常测试只读取这两份基线，不重新生成期望值。


2026-11-04 的测试与架构重构冻结当前 `src/core/` 两个文件的原字节，并通过 `cmake/VerifyFrozen.cmake` 校验上述历史基线。新增 `tests/fixtures/input_equivalence.json` 来源为生产改动前的 `e73f4c6` 输入实现（观察能力见保护提交 `d84425d`）；记录双虚拟输出混合缩放、每份事件位置与诊断轨迹，以及队列溢出回退。新增基线不替代原来的参考模型或历史数据，正常测试不提供重生成入口。
