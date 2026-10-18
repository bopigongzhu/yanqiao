# 第三方来源与开发包边界

本地开发构建使用以下组件。直接依赖的下载地址和 SHA-256 见 `dependencies.lock.json`；补充许可的原文地址与文件哈希见 `licenses/sources.json`（打包后位于同目录 `sources.json`）。许可文本随开发包保存。

| 组件 | 来源 / 版本 | 许可 |
|---|---|---|
| librime | `rime/librime`，1.17.0 / `33e7814` | 核心 BSD-3-Clause；预编译包包含下列插件 |
| 简体全拼词典 | `rime/rime-pinyin-simp`，`0c6861ef7420ee780270ca6d993d18d4101049d0` | Apache-2.0 |
| nlohmann/json | 3.12.0 | MIT |
| LLVM / libc++ 运行库 | llvm-mingw 20260922 | Apache-2.0 WITH LLVM-exception 等，见 LLVM 原文 |
| mingw-w64 运行库 | llvm-mingw 所带版本 | 见 COPYING；所存声明参考 v13.0.0，准确构建修订仍需核对 |
| glog、LevelDB、yaml-cpp、marisa、OpenCC、Boost、darts-clone | librime 对应源码的依赖提交及 Windows 构建工作流 | 已收集对应原文；marisa 可按 BSD 选项使用 |
| librime-lua / Lua | `ec52e48`；Lua 5.4 系列 | BSD-3-Clause / MIT；Lua 许可取自 5.4.8 的 lua.h，二进制补丁版本未独立核实 |
| librime-predict | `920bd41` | BSD-3-Clause |
| **librime-octagram** | **`dfcc151`** | **GPL-3.0** |

`rime-version-info.txt` 来自实际下载的 `rime-33e7814-Windows-clang-x64.7z`。该预编译 DLL 将 octagram 等插件编入同一库，即使本项目的全拼方案没有启用它，也不能将整个 DLL 视为仅受 BSD 许可覆盖。

当前包用于此工作区的本地开发与验证，尚未作为对外发行包发布。对外分发前，需要改用经过验证的不含这些可选插件的 Rime 构建，或完成对应许可证及完整对应源码的分发安排；仅附上此清单并不能完成 GPL 分发义务。本项目自己的发布许可证尚未替用户选择。

原始资料：

- https://github.com/rime/librime/tree/33e7814
- https://github.com/rime/librime/blob/33e7814/.github/workflows/windows-build.yml
- https://github.com/lotem/librime-octagram/tree/dfcc151
- https://github.com/rime/rime-pinyin-simp/tree/0c6861ef7420ee780270ca6d993d18d4101049d0

原始 `rime-LICENSE`、`json-LICENSE`、`pinyin-simp-LICENSE` 保留版权及免责条款。词典以原始内容分发，`e_shuru.schema.yaml` 是本项目的独立配置，禁用用户词库学习。
