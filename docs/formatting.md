# 代码格式

C/C++ 批量整理使用 clang-format，VS Code 中手写代码继续使用 Microsoft C/C++ 扩展的 vcFormat。两者保持接近的风格：Tab 缩进、显示宽度 4、命名空间内部不额外缩进、大括号独占一行，展开短函数、条件分支及多语句行。长表达式和参数列表以 120 列为换行目标，字符串内容保持原样。

## 项目配置

- `.clang-format` 保存命令行批量格式化规则，基于 Microsoft 风格；保留包含文件顺序和注释，函数之间留空行。
- `.editorconfig` 保存基本缩进规则及对应的 vcFormat 风格。Shell 使用 Tab，Python 使用 4 个空格，CMake 使用 2 个空格。
- `.cmake-format.json` 保存 CMake 的 2 空格缩进和 120 列换行设置，同时关闭参数排序和注释重排。

Python 和 QML 保留本次已整理的排版，不增加专用格式化工具或检查要求。

## 安装工具

Fedora 44 可以直接安装以下软件包；`clang-tools-extra` 提供 clang-format，`cmakelang` 提供 cmake-format。

```bash
sudo dnf install clang-tools-extra shfmt cmakelang
```

本次使用 clang-format 22.1.8、shfmt 3.7.0 和 cmakelang 0.6.13。shfmt 的部分发行版构建不输出 `--version`，可用 `rpm -q shfmt` 查询包版本。

## 批量格式化

在项目根目录执行以下命令。文件列表来自 Git，构建产物和生成文件不参与排版；头文件模板按 C++ 处理。

```bash
git ls-files -z -- '*.c' '*.cpp' '*.h' '*.hpp' '*.hpp.in' |
  xargs -0 -r clang-format --assume-filename=source.cpp -i

git ls-files -z -- '*.sh' | xargs -0 -r shfmt -w
cmake-format -i CMakeLists.txt
```

shfmt 从 `.editorconfig` 读取 Bash 方言和 Tab 缩进设置。新增分支、循环及连续命令应展开到多行；它会保留部分手写的单行结构。

## 检查

```bash
git diff --check

git ls-files -z -- '*.c' '*.cpp' '*.h' '*.hpp' '*.hpp.in' |
  xargs -0 -r clang-format --assume-filename=source.cpp --dry-run --Werror

git ls-files -z -- '*.sh' | xargs -0 -r shfmt -d
cmake-format --check CMakeLists.txt

for script in install.sh uninstall.sh scripts/*.sh; do
  bash -n "$script"
done

cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

重复运行批量格式化后应无变化。整理时保留宏逻辑、字符串、heredoc、JSON 字段和 XML 节点的内容及顺序。
