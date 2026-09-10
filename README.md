# Motor Driver Controller — 发布页
- 在线发布页：https://mumu-elec.github.io/mdc-pub

基于 STM32F401 + TB6612 的四路直流电机驱动器（Bootloader + 主固件 + Web 上位机）的**发布仓库**。

## 版本清单（releases.json）— 单一事实来源

- 发布页与上位机「联网版」都从根目录的 [`releases.json`](releases.json) 读取版本数据。
- **发新版只需：放文件进 `release/` → 改 `releases.json` → 推送**，index.html 会自动渲染，无需改页面。
- 上位机联网版功能（在线固件库 / 自动检查更新 / 协议不匹配引导）也读这份清单，地址写在 `online.js` 顶部的 `MANIFEST_URL`。
- 上位机形态说明：文件名带 `-online` 后缀 = 联网版（在线固件库 / 更新检查 / 配套引导，断网自动降级）；无后缀 = 完全离线版（零网络请求，功能相同）。

## 在线上位机（web/ 统一目录 · 按协议线分子目录）

- v2 线（协议 D=2，当前线）：https://mumu-elec.github.io/mdc-pub/web/v2/ · 历史按版本号：`web/v2/v2.0.html`
- v1 线（协议 D=1，存档）：https://mumu-elec.github.io/mdc-pub/web/v1/ · 历史按版本号：`web/v1/v1.1.html`
- 结构规则：`web/<线>/index.html` = 本线**最新**联网版上位机；同目录另存文件名为版本号的历史联网版
- 需要 Chrome / Edge 浏览器（Web Serial API）

## 当前发布物

| 类别 | 版本 | 文件 |
|------|------|------|
| Web 上位机 | v2.0 | `release/电机上位机-v2.0.html`（离线）/ `release/电机上位机-v2.0-online.html`（联网）/ 在线 `web/` |
| 主固件 | v1.2.0 | `release/motor_driver_ctrl-v1.2.0.bin` / `.hex`（底盘 / 单电机一体，协议 D=2） |
| v1 存档固件 | v1.1.0 / v1.1.1 / v1.1.2 | `release/motor_driver_ctrl-v1.1.*.bin` / `.hex`（协议 D=1，配上位机 v1.x） |
| Bootloader | v1.0 | `release/bootloader-v1.0.bin` |
| 技术手册 | v2.0 | `docs/technical-manual.md`（硬件规格 / 接口 / 协议 / 底盘功能 / OTA） |
| 例程库 | 协议 D=2 | `mdc_lib/`（Python / C++ / ROS / MicroPython / 单片机全平台例程 + 通信协议规范） |

> 版本规则：固件 vA.D.E（A=硬件 D=协议 E=补丁）· 上位机 vD.F（D=协议 F=修复）。上位机与固件的协议版本 D 必须一致；不匹配时联网版上位机会自动引导切换到配套版本。

## 目录结构

```
motor_driver_control/
├── index.html              # 发布页（读 releases.json 渲染全版本）
├── releases.json           # 版本清单（发布页 + 上位机联网版 共用的单一数据源）
├── web/                    # 在线上位机统一目录（联网版单文件，自包含，按协议线分子目录）
│   ├── v2/index.html       #   v2 线最新上位机（index 恒等于本线最新）；v2/v2.0.html = 按版本号存历史
│   └── v1/index.html       #   v1 线最新上位机；v1/v1.1.html = 按版本号存历史
├── release/                # 发布物（上位机 online/离线 / 固件 / BL）
├── docs/                   # 技术手册（v2.0）+ 配图 image/
└── mdc_lib/                # 例程库（多平台 + 协议规范）
```

## 更新发布物

1. 新构建产物放入 `release/`（上位机由打包脚本产出联网版 + 离线版两个文件）。
2. 在 `releases.json` 对应线（line）的 `releases` 数组**末尾追加**新版本条目（fw 版本 / 日期 / 说明 / 固件文件 / 配套上位机）。
3. 提交推送，GitHub Pages 自动生效；发布页与上位机联网版即读到新版本。
