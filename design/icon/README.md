# 言桥 · 应用图标

主方案：**Y bridge mark**  
Y 是“言桥 / YanQiao”的首字母，也是斜拉桥的**桥塔 + 两根拉索**；底部薄荷色横线是被拉索拉住的**桥面**。浅暖色底板更适合长期停留在桌面和任务栏，深色底板作为高对比备选。

上一版单拱方案在 16 px 下容易读成马蹄铁/磁铁；这一版把结构收成 Y 形，16 px 仍能保持清楚的“塔 + 桥面”轮廓。

## 文件

| 文件 | 用途 |
|---|---|
| `yanqiao.ico` | 浅色主图标，Windows 16 / 24 / 32 / 48 / 64 / 128 / 256 |
| `yanqiao-dark.ico` | 深色备选，同一组尺寸 |
| `yanqiao.svg` / `yanqiao-dark.svg` | 矢量主稿，256 × 256 |
| `yanqiao-16.png` … `yanqiao-512.png` | 浅色各尺寸 PNG |
| `yanqiao-dark-16.png` … `yanqiao-dark-512.png` | 深色各尺寸 PNG |
| `yanqiao-master.png` / `yanqiao-dark-master.png` | 2048 px 母版 |
| `preview-final.png` | 浅色 / 深色 256 px 与 16–64 px 实际尺寸对照 |
| `make_final.py` | 生成最终资产的脚本 |
| `alternatives/` | 早期拱桥、悬索桥、对话气泡、A 形桥等方向 |
| `explore2/` | 第二轮探索的中间 PNG |

## 设计参数

- 画布 256 × 256；底板 8 → 248，圆角 58。
- 浅色底板：`#FBFAF5` → `#EFECE2`，1.5 px 低对比描边；深色底板：`#2D6F58` → `#184133`。
- Y / 桥塔：`#1F4D3F`（浅色）或 `#F7F5EE`（深色），主线宽 33，圆头。
- 桥面：`#4F9B7A`（浅色）或 `#9AD6B8`（深色），40 → 216，高 11，圆角 5.5。
- Y 的竖向塔身停在桥面处，不穿到底，避免在 16 px 读成高脚杯/鸡尾酒杯。
- 无文字、无细高光、无多层阴影；16 px 只有塔、两根斜索、一条桥面。

## 接入位置（尚未修改程序）

1. **EXE 图标 / 任务管理器**：给 `EShuRu` 加 `.rc`，例如  
   `IDI_APP_ICON ICON "design/icon/yanqiao.ico"`，窗口类使用 `LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON))`。
2. **托盘图标**：[src/app.cpp](../../src/app.cpp) 目前使用 `LoadIcon(nullptr, IDI_APPLICATION)`；改为加载同一资源即可。深色任务栏可选用 `yanqiao-dark.ico`。
3. **窗口图标**：`WNDCLASSW.hIcon` / `hIconSm` 指向同一资源。
4. **TSF 语言列表图标**：`EShuRuTip.dll` 增加图标资源；`DllRegisterServer` 的 `AddLanguageProfile` 使用 DLL 路径，Windows 取图标索引 0。
5. **安装包**：`.ico` 随源码保留；编译进 EXE/DLL 即可，不必额外复制。

## 已接入

- [src/app.ico](../../src/app.ico) 是编译用副本；[src/app.rc](../../src/app.rc) 与 [src/resource.h](../../src/resource.h) 定义 `IDI_APP_ICON`。
- CMake 把同一个资源编进 `EShuRu.exe`、`EShuRuMaintenance.exe` 和 `EShuRuTip.dll`；x64/x86 的 RC 目标格式分别固定为 `pe-x86-64` / `pe-i386`。
- `EShuRu.exe` 的 broker 窗口类、设置窗口、试用窗口和托盘都使用 `IDI_APP_ICON`；[src/app.cpp](../../src/app.cpp) 的托盘不再使用系统默认图标。
- `EShuRuTip.dll` 含同一图标；安装脚本把语言 profile 的 `IconFile` 指向新的 x86 DLL，`IconIndex` 为 0。
- 当前统一使用浅色主图标；`yanqiao-dark.ico` 尚未按系统主题动态切换。
