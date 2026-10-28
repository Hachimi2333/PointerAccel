# 算法来源与许可证

PointerAccel 保留了 [windows-pointer-linux](https://github.com/junaga/windows-pointer-linux) 的最小算法引擎和回归测试，直接参与本项目构建。无需克隆原项目，也不依赖原项目文件夹。

- 上游版本：1.0.0。
- 来源提交：[`0bdd644d420737a7aea506e252fc5e12db34c599`](https://github.com/junaga/windows-pointer-linux/tree/0bdd644d420737a7aea506e252fc5e12db34c599)。
- 许可证：[BSD-3-Clause](../LICENSES/windows-pointer-linux.txt)，原版权声明完整保留。
- 2026-10-28 整理目录时，以下文件按字节复制，并在移除原工作副本前核对 SHA-256。没有修改算法、头文件或测试内容。

| 本项目路径 | 原项目路径 | SHA-256 |
| --- | --- | --- |
| `src/windows_pointer/engine.cpp` | `src/engine.cpp` | `698a09996ae6086e5414da23c84ca2f8c0156ffd99a6a5ba6ec8484c99c6950e` |
| `include/windows_pointer/engine.hpp` | `include/windows_pointer/engine.hpp` | `bfb4a1969ca0dd22af632c9796040cc4ce136ae04d8eff8a9f0f138aac809ff7` |
| `tests/windows_pointer/engine_test.cpp` | `tests/engine_test.cpp` | `f4801b364ade52f89a2a66c7c78f0c2235fb031b3932f06e81ada87538884d3a` |
| `tests/windows_pointer/model_test.cpp` | `tests/model_test.cpp` | `537253e27ee40041b6afb7e300e49b0538345267175376a1455b5304389d299b` |
| `tests/windows_pointer/scenario_test.cpp` | `tests/scenario_test.cpp` | `056ae7ebd93b2c2db8a05b88a35bfa09598a6c1c49cbb2a680a315a623605512` |
| `tests/windows_pointer/reference_model.hpp` | `tests/reference_model.hpp` | `fc6ddf3a5ce7a14b172015835ef6f00a50796c3800ee97cbdb23743dfd03ad54` |
| `LICENSES/windows-pointer-linux.txt` | `LICENSE` | `6e32a6f6623073fc97f5d043c253e9ef566ac534ff438a2780f56abc0c740990` |

原项目的 Hyprland 适配器、实验环境和其他工具未包含在本项目中。PointerAccel 的 KWin 适配、CLI、安装卸载脚本及诊断工具由本项目维护。两层之间仍以 `windows_pointer::Engine` 为边界。
