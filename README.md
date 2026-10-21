# DeLSS · 反向 DLSS

> DLSS 5 把游戏画面变得更像照片，DeLSS 反过来：把写实画面实时变成卡通、像素、PS1、Game Boy、CRT/VHS、美漫和老照片。

**在线演示：<https://sdobili.github.io/DeLSS/>**（不依赖任何 CDN，国内可以直接打开）

[![DeLSS 宣传片 / promo video](web/promo/poster.jpg)](https://sdobili.github.io/DeLSS/promo/delss-promo.mp4)

> ▶ 宣传片 Promo video：<https://sdobili.github.io/DeLSS/>（纯属整活 · parody, not affiliated with NVIDIA）

![对比总览](docs/gallery/grid.jpg)

## 概念

DLSS 5（NVIDIA，2026 年 9 月）是渲染管线的最后一步：输入引擎画面和运动向量，输出更写实的光照和材质，内容和美术意图保持不变。DeLSS 在同一个位置做相反的事。

| | DLSS 5 | DeLSS |
|---|---|---|
| 在管线里的位置 | 最后一步，后处理 | 最后一步，后处理 |
| 输入 | 颜色 + 运动向量 | 颜色（必需）+ 深度 / 法线 / 运动向量（可选） |
| 输出 | 更写实 | 更风格化 / 更复古 |
| 时域稳定 | 依靠运动向量 | 屏幕空间锚定的有序抖动，加上按运动向量重投影的防闪烁 pass |
| 美术控制 | 强度、调色、遮罩 | 每个 pass 都有强度，以及调色板、像素大小、描边宽度、扫描线等参数 |

## 风格

左半边是原图，右半边是 DeLSS 的输出。3D 场景这一列带有深度和法线，所以描边来自几何；照片没有 G-buffer，描边改用亮度检测。

| 风格 | 照片 | 3D 场景（带 G-buffer） |
|---|---|---|
| 赛璐璐 `cel` | ![](docs/gallery/street_cel.jpg) | ![](docs/gallery/scene_cel.jpg) |
| 美漫 `comic` | ![](docs/gallery/street_comic.jpg) | ![](docs/gallery/scene_comic.jpg) |
| 像素 PICO-8 `pixel` | ![](docs/gallery/cat_pixel.jpg) | ![](docs/gallery/scene_pixel.jpg) |
| PS1 `ps1` | ![](docs/gallery/knight_ps1.jpg) | ![](docs/gallery/scene_ps1.jpg) |
| Game Boy `gameboy` | ![](docs/gallery/cat_gameboy.jpg) | ![](docs/gallery/scene_gameboy.jpg) |
| CGA 1984 `cga` | ![](docs/gallery/knight_cga.jpg) | ![](docs/gallery/scene_cga.jpg) |
| 显像管 `crt` | ![](docs/gallery/cat_crt.jpg) | ![](docs/gallery/scene_crt.jpg) |
| 录像带 `vhs` | ![](docs/gallery/street_vhs.jpg) | ![](docs/gallery/scene_vhs.jpg) |
| 老照片 `oldphoto` | ![](docs/gallery/cat_oldphoto.jpg) | ![](docs/gallery/scene_oldphoto.jpg) |

每个风格都是由几个 pass 组合成的预设，定义在 [`shaders/presets.json`](shaders/presets.json) 里：

| Pass | 作用 | 主要参数 |
|---|---|---|
| `pixelate` | 按格子做盒式平均，变成大像素 | 像素大小 |
| `palette` | 调色板量化（OKLab 最近色，或按亮度映射），加有序抖动 | 调色板、色阶、抖动强度、抖动格大小、Bayer / PS1 抖动矩阵 |
| `kuwahara` | 保边平滑，把照片纹理变成平涂色块 | 半径 |
| `cel` | 亮度分档，保留色相 | 档数、过渡柔和度、饱和度、亮度 |
| `ink` | 描边：有 G-buffer 时用深度和法线，没有时用亮度 | 线宽、阈值、墨色、深度 / 法线权重 |
| `halftone` | 网点印刷，网格固定在屏幕上 | 网点大小、网角、纸色 |
| `crt` | 曲面、扫描线、荫罩、暗角 | 曲率、扫描线强度、行高、荫罩、辉光 |
| `vhs` | 色度带宽不足导致的拖影、错位、跟踪噪声 | 色度模糊、偏移、噪声、锐化 |
| `grade` | 老照片调色：棕褐、褪色、暗角、颗粒 | 棕褐、饱和度、对比度、颗粒 |
| `temporal` | 防闪烁：按运动向量重投影上一帧的输出 | 稳定度、容差、块大小 |

## 架构

```mermaid
flowchart LR
  subgraph SRC["唯一来源 shaders/"]
    G["common.glsl<br/>输入约定和公共函数"]
    P["passes/*.frag<br/>10 个 GLSL ES 3.00 pass"]
    M["passes.json / palettes.json / presets.json<br/>参数、调色板、预设"]
  end
  GEN["tools/gen.py<br/>校验清单和 uniform 是否一致"]
  SRC --> GEN
  GEN --> JS["web/src/generated.js"]
  GEN --> HPP["cpp/include/delss/generated.hpp"]
  JS --> WEB["WebGL2 运行时<br/>web/src/delss.js"]
  HPP --> CPP["C++ 参考库<br/>delss::Pipeline (GLES 3.0)"]
  WEB --> DEMO["在线演示<br/>GitHub Pages"]
  CPP --> CLI["delss-cli / 游戏引擎集成"]
  subgraph FRAME["每一帧"]
    direction LR
    IN["color + depth? + normal? + motion?"] --> C1["pass 1"] --> C2["pass 2"] --> CN["…"] --> T["temporal<br/>读 history"] --> OUT["风格化输出"]
    OUT -. "下一帧的 history" .-> T
  end
```

- **着色器只有一份**：`shaders/` 是唯一的来源。`tools/gen.py` 把公共头内联进每个 pass，检查 `passes.json` 里的每个参数都有对应的 `u_<名字>` uniform，反过来也一样，再生成 JS 和 C++ 的绑定代码。CI 会检查生成文件有没有过期。
- **两个运行时，同样的像素**：C++ 库在 Mesa llvmpipe 上渲染每个 pass 和预设，和 WebGL2 在 SwiftShader 上生成的金图逐张比对，30/30 一致。

### 输入约定

| 纹理 | 是否必需 | 内容 |
|---|---|---|
| `color` | 必需 | 引擎最终颜色，sRGB，RGBA8 |
| `depth` | 可选 | r = 线性视深度 / far，范围 [0,1] |
| `normal` | 可选 | rgb = 视空间法线 × 0.5 + 0.5 |
| `motion` | 可选 | rg = 当前 uv − 上一帧 uv，建议用 RGBA16F |

纹理按 GL 习惯，(0,0) 在左下角。缺少的通道各 pass 会自动降级，例如 `ink` 没有深度和法线时改用亮度描边。

## 时域稳定性

量化是不连续的：输入里差一个 8 位色阶的噪声，就可能让像素在两个调色板颜色之间来回跳。DeLSS 用三层手段压住闪烁：

1. **有序抖动锚定在屏幕格上**。Bayer 和 PS1 抖动矩阵只取决于整数格坐标，静止画面得到的图案也是静止的。误差扩散（Floyd–Steinberg）在任何一个像素变化时都会波及整帧，所以不用。
2. **几何描边**。有 G-buffer 时，描边来自深度和法线的不连续，不受光照和纹理噪声影响。
3. **`temporal` pass**。用运动向量把上一帧的输出重投影过来。如果它落在当前帧邻域的颜色包围盒里，就保留它。保留的仍然是调色板里的颜色，所以不会像 TAA 那样变糊；包围盒检查能防止拖影。邻域按"块大小"采样，像素画会按整个大像素来判断，不会把一个大像素拆成两半。重投影的偏移取整到整块，并且用 `texelFetch` 读取不经过滤的 history，所以镜头移动时大像素仍然对齐屏幕网格，也不会混出调色板以外的颜色。

测试里给照片加 ±6 的随机噪声，连续 8 帧，统计相邻帧之间变化的像素比例：

| 预设 | 不加 temporal | 加 temporal |
|---|---|---|
| `pixel` | 2.36% | 0.17% |
| `gameboy` | 0.96% | 0.05% |
| `cel` | 79.3% | 3.8% |

另有一条测试：输入完全静止时，除 VHS 以外所有预设的输出必须逐像素不变。VHS 的跟踪噪声是故意加的，噪声参数设为 0 就完全静止。

## 集成

### WebGL2

```js
import { DeLSS, presetChain } from './src/delss.js';

const delss = new DeLSS(gl);
delss.setChain(presetChain('pixel'));
// 可以直接改参数：delss.chain[0].params.pixelSize = 6

function onFrame() {
  delss.render(
    { color: colorTex, depth: depthTex, normal: normalTex, motion: motionTex }, // 后三个可省略
    { width, height, target: null }, // null = 直接画到 canvas
  );
}
```

也可以自己组合 pass：

```js
delss.setChain([
  { pass: 'kuwahara', params: { radius: 4 } },
  { pass: 'cel', params: { bands: 3 } },
  { pass: 'ink', params: { width: 2, inkColor: '#202040' }, intensity: 0.8 },
  { pass: 'temporal' },
]);
```

拼错 pass 名或参数名会直接抛错，不会悄悄渲染出错的画面。

### C++ / OpenGL ES 3.0

```cpp
#include "delss/delss.hpp"

delss::Pipeline pipeline;  // 需要当前线程有 GLES 3.0 上下文
pipeline.setChain(delss::presetChain("gameboy"));

// 每帧：
pipeline.render({colorTex, depthTex, normalTex, motionTex}, width, height, /*targetFbo=*/0);
```

```cmake
add_subdirectory(DeLSS/cpp)  # 只要库本身：-DDELSS_BUILD_TOOLS=OFF
target_link_libraries(my_game PRIVATE delss::delss)
```

桌面 OpenGL 4.3 及以上通过 `ARB_ES3_compatibility` 也能直接跑这些 ES 3.0 着色器。

### 命令行

```bash
cmake -S cpp -B build -G Ninja && cmake --build build
./build/delss-cli --list
./build/delss-cli --preset comic photo.png out.png
./build/delss-cli --preset cel --depth d.png --normal n.png color.png out.png
```

### 新增一个 pass

1. 写 `shaders/passes/<name>.frag`，参数声明成 `uniform float u_<param>;`，最后调用 `delssOut(styled)`，强度滑杆会自动生效。
2. 在 `shaders/passes.json` 里登记参数的默认值和范围。演示页的调参面板会自动生成。
3. 运行 `python3 tools/gen.py`，再运行 `npm run golden:update`，然后检查新增的金图。

## 全屏滤镜 delss-overlay

不用接入任何游戏：打开 `delss-overlay`，按 **Ctrl+Alt+D**，当前屏幕上的所有内容都会被实时风格化，不管底下是什么应用。

![X11 下运行的 delss-overlay](docs/overlay-x11.png)

原理：抓屏（排除自己的窗口，避免回授）→ 同一套 DeLSS 着色器 → 画到每个显示器上一个置顶、无边框、鼠标穿透的全屏窗口。

| 系统 | 抓屏 | 覆盖窗口 | 热键 | 托盘 |
| --- | --- | --- | --- | --- |
| Windows 10 2004+ | DXGI 桌面复制 | `WS_EX_LAYERED \| TRANSPARENT \| TOPMOST` + `WDA_EXCLUDEFROMCAPTURE` | GetAsyncKeyState | 通知区图标 |
| macOS 12.3+ | ScreenCaptureKit（过滤器排除自己的窗口） | `NSScreenSaverWindowLevel` + `ignoresMouseEvents` | Carbon 热键（Ctrl+Option） | 菜单栏 |
| Linux X11 | XComposite（逐个叠顶层窗口，跳过自己） | override-redirect + 空 XShape 输入区 | XGrabKey | AppIndicator |
| Linux Wayland | 不支持全局抓屏，改用合成器着色器（见下） | — | — | — |

### 安装和使用

从 [Actions 的 Overlay 工作流](https://github.com/Sdobili/DeLSS/actions/workflows/overlay.yml) 下载对应系统的构建产物，或自己编译：

```bash
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release -DDELSS_BUILD_OVERLAY=ON
cmake --build build --config Release
```

- **Windows**：直接运行 `delss-overlay.exe`，不需要任何权限。
- **macOS**：打开 `DeLSS Overlay.app`（未公证，首次需右键→打开）。第一次会请求“屏幕录制”权限：系统设置 → 隐私与安全性 → 屏幕录制，勾选后重启应用。没有权限时滤镜会在 3 秒后自动隐藏。
- **Linux X11**：运行 `delss-overlay`，需要 Composite 扩展（几乎所有 X 服务器都有）。Linux 依赖：`libxcomposite libxext`，托盘可选 `libayatana-appindicator3`。
- **Linux Wayland**：Wayland 不允许普通程序抓全屏或盖在所有窗口上。xdg-desktop-portal ScreenCast + PipeWire 这条路这一版没做（每次都要弹门户授权，而且盖在上面的窗口仍然没法置顶穿透）。目前的办法是用合成器自带的屏幕着色器：`compositor/hyprland/delss-<风格>.frag` 由 `tools/compositor.py` 从同一份着色器生成，在 `hyprland.conf` 里写 `decoration:screen_shader = /路径/delss-cel.frag`。只支持 Hyprland；GNOME、KDE 的 Wayland 会话目前不支持，可以切到 X11 会话。

热键（macOS 上 Alt 是 Option）：

| 按键 | 作用 |
| --- | --- |
| Ctrl+Alt+D | 开 / 关 |
| Ctrl+Alt+P / O | 下一个 / 上一个风格 |
| Ctrl+Alt+↑ / ↓ | 强度 ±10% |
| Ctrl+Alt+H | 显示 / 隐藏左上角每帧耗时 HUD |
| **Ctrl+Alt+Esc** 或 Ctrl+Alt+Q | 安全退出 |

覆盖窗口永远是鼠标穿透的，不会挡住任何点击；抓屏连续失败 3 秒会自动隐藏。托盘菜单里可以选风格、强度、显示器和 HUD。设置自动保存在 `%APPDATA%\DeLSS\overlay.ini`、`~/Library/Application Support/DeLSS/overlay.ini` 或 `~/.config/delss/overlay.ini`。命令行参数见 `delss-overlay --help`。

### 画质取舍

抓屏只拿得到颜色，没有深度、法线和运动向量，所以：描边改用亮度边缘检测，复杂纹理处会多出杂线；时间稳定只能用上一帧结果混合，镜头快速移动时比引擎接入版更容易拖影或闪烁。另外抓屏要先拷到 CPU 再上传，4K 下开销明显。想要最好的效果，还是用 C++ 库或 WebGL 版本接入渲染管线。

### 反作弊

delss-overlay 不注入游戏进程，也不读写游戏内存，只是抓屏再盖一个窗口，和录屏、直播软件是同一类做法，一般是安全的。但各家反作弊规则不同，在线竞技游戏请自行斟酌。部分**独占全屏**的游戏会绕过覆盖窗口（直接看不到滤镜），请把游戏改成**无边框窗口**模式。

## 开发和测试

```bash
npm ci && npx playwright install chromium
npm run serve          # http://127.0.0.1:8080
npm test               # WebGL 金图、契约、时域稳定性、演示页冒烟测试
npm run lint           # eslint + prettier
python3 tools/gen.py --check
cmake -S cpp -B build -G Ninja && cmake --build build && ctest --test-dir build
python3 tools/gallery.py   # 重新生成 README 里的对比图
```

| 检查 | 内容 |
|---|---|
| `tests/web/golden.test.mjs` | 每个 pass、每个预设的金图（headless Chromium + SwiftShader），空链路和强度为 0 时完全旁路，非法名字报错，闪烁指标 |
| `tests/web/demo.test.mjs` | 演示页在两个输入源上切换所有预设，没有报错，HUD 显示帧时间 |
| `cpp/tests/golden_test.cpp` | C++ 在 Mesa llvmpipe 上的输出和 WebGL 的金图一致 |
| 代码检查 | ESLint、Prettier、Ruff、clang-format、clang-tidy（警告视为错误） |

金图比较允许实现之间的取整误差：平均误差小于 1/255；超过 16/255、并且在参考图 3×3 邻域里也找不到匹配的像素，比例要低于 0.5%。

## 目录

```
shaders/        唯一来源：common.glsl、passes/*.frag、passes.json、palettes.json、presets.json
tools/          gen.py（生成绑定）、make-fixtures.mjs、gallery.py、serve.mjs
web/            WebGL2 运行时（src/）、演示页（index.html、demo/）、示例图（samples/）
cpp/            C++ 库（include/、src/）、无头 EGL 工具和 CLI（apps/）、测试（tests/）
tests/          测试输入（fixtures/）、WebGL 金图（golden/web/）、Node 测试
docs/gallery/   README 用的对比图
```

## 已知不足

- 后处理拿不到顶点，所以 PS1 的顶点抖动和仿射贴图扭曲做不出来，`ps1` 预设只还原了 15 位色、硬件抖动和低分辨率。
- 抖动锚定在屏幕上，不跟着物体走。`temporal` pass 能让图案跟着运动向量保持一段时间，但没有真正做"物体空间抖动"。
- 没有 ReShade `.fx` 版本。ReShade 用的是 HLSL 风格的语法，要单独维护一份移植，会破坏"着色器只有一份"。
- 只在 CPU 光栅器（SwiftShader、llvmpipe）上验证过，还没在真实 GPU 上测过帧时间。在线演示的 HUD 会显示你机器上的实测 GPU 时间。

## 素材

`web/samples/` 下的照片由 AI 生成，3D 场景由 `web/demo/scene.js` 里的光线步进着色器实时渲染，都没有第三方版权。代码采用 MIT 许可，`cpp/third_party/` 下的 stb 是公有领域。

---

> **免责声明 / Disclaimer**  
> 纯属整活，非官方作品，与 NVIDIA 无关。DLSS 是 NVIDIA 的商标。  
> Parody project. Not affiliated with or endorsed by NVIDIA. DLSS is a trademark of NVIDIA Corporation.
