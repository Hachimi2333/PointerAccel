# 安装、升级与恢复

[返回项目首页](../README.zh-CN.md) · [使用指南](usage.md)

## 安装与登录

请按首页的依赖说明准备环境，再以普通用户运行 `./install.sh`。脚本先校验 KWin 运行包与开发包，构建并运行测试，成功后才开始安装；安装系统模块与兼容记录时请求 sudo 权限。

安装成功后，保存工作并手动注销、重新登录 Plasma。脚本不安装依赖包，也不自动重启桌面。

登录后，用户服务先校验安装文件与 KWin 构建，再请求 KWin 加载模块。`kwinrc` 中的 `pointeraccel_kwinEnabled` 保持关闭，由检查服务负责加载，这是正常配置。检查失败时使用原生输入，并通过桌面通知和 `pointeraccel doctor` 报告原因；检查服务不链接 `libkwin`，不会在后台编译或提权。

## KWin 升级与修复

模块二进制绑定具体 KWin 构建：版本 IID、库与可执行文件的 SHA-256 都必须匹配。同一版本的发行版重打包也可能需要重新编译。升级后运行：

```bash
pointeraccel status --json
pointeraccel repair
```

修复默认使用安装时保存的源码快照，原项目目录移动或删除后仍可使用。它在独立缓存目录构建并运行完整测试，成功后调用 sudo 安装；不自动拉取代码或安装依赖。若需要使用修改后的源码：

```bash
pointeraccel repair --source /path/to/PointerAccel
```

修复成功后手动注销并重新登录。程序不会把新模块热加载到当前 KWin，也不会自动重启桌面。从旧版直接自启布局升级时，在源码目录运行 `./install.sh` 迁移到检查服务。

从仅支持 KWin 6.7 的旧版本升级到 6.8 时，应先在更新后的项目目录运行 `./install.sh`。旧安装的默认修复使用旧源码快照，无法获得新增的 6.8 适配；安装更新后的版本会同时更新 CLI、检查服务程序和修复快照，并保留用户设置。

| 状态字段/值 | 含义 |
| --- | --- |
| `running_compatible` | 当前 KWin 与已加载模块是否匹配 |
| `startup_compatible` | 磁盘上的 KWin 与安装模块是否匹配 |
| `startup_state: ready` | 当前登录允许检查服务加载模块 |
| `startup_state: pending-login` | 安装/修复已完成或运行版本落后，需要重新登录 |
| `startup_state: repair-required` | 支持的 6.7／6.8 系列构建已变化，需要重建 |
| `startup_state: unsupported` | KWin 不属于已支持的 6.7／6.8 系列，需要代码适配 |
| `startup_state: blocked` | 安装文件、兼容清单或事务异常，已阻止加载 |
| `repair_required` / `relogin_required` | 分别表示需要修复或重新登录 |

在线升级时，旧 KWin 与旧模块可以继续正常工作，因此可能同时看到 `active: true` 和 `repair_required: true`。模块运行时核验进程内的 GNU Build ID，配置变更不会仅因磁盘更新而停用旧模块。下次登录若不匹配则不加载模块，鼠标恢复系统原生行为。

KWin 6.9 及以后明确拒绝构建和自动加载；重新编译不能代替接口适配。检查服务监听会话和文件变化，每 30 秒补查一次文件属性，仅变化时重新计算哈希；相同故障和构建组合的桌面通知去重。

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
| `~/.local/share/pointeraccel/` | CLI、独立 GUI、诊断工具、卸载脚本、源码快照、说明与许可证 |
| `~/.local/bin/pointeraccel` | 指向 CLI 的符号链接 |
| `~/.config/pointeraccel/config.json` | 用户设置 |
| `~/.local/state/pointeraccel/` | 文件清单、配置备份、安装会话与通知去重记录 |
| `~/.config/systemd/user/pointeraccel-session.service` 及其 Plasma target 链接 | 当前用户的兼容检查与延迟加载服务 |
| `~/.config/kwinrc` 的 `[Plugins] pointeraccel_kwinEnabled` | 设为 false，交由检查服务加载；卸载恢复原值 |

用户数据目录遵循 `XDG_DATA_HOME`、`XDG_CONFIG_HOME` 和 `XDG_STATE_HOME`。安装和卸载时应保持这些变量一致。系统目录固定为本项目验证过的 Fedora 布局。

不会改写系统 KWin/libinput 文件，不修改其文件能力，不修改 `kcminputrc`，不创建用户服务覆盖，也不关闭 SELinux 或 Secure Boot。
