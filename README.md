# Motor Driver Controller — 发布页

基于 STM32F401 + TB6612 的四路直流电机驱动器（Bootloader + 主固件 + Web 上位机）的**发布仓库**。

## 在线上位机

- 在线使用（GitHub Pages）：https://mumu-elec.github.io/mdc-pub/web/
- 需要 Chrome / Edge 浏览器（Web Serial API）

## 发布物

| 类别 | 版本 | 文件 |
|------|------|------|
| Web 上位机 | v1.0 | `release/电机上位机-v1.0.html`（离线单文件） |
| 主固件 | v1.1.0 | `release/motor_driver_ctrl-v1.1.0.bin` / `.hex` |
| Bootloader | v1.0 | `release/bootloader-v1.0.bin` |
| 技术手册 | v1.0 | `docs/technical-manual.md` |

## 目录结构

```
motor_driver_control/
├── index.html              # 发布页（下载中心 + 网站入口）
├── web/index.html          # 在线上位机（单文件，自包含）
├── release/                # 发布物（上位机 / 固件 / BL）
└── docs/                   # 技术手册（v1.0）
```

## 更新发布物

将新构建的发布物放入 `release/`，更新 `index.html` 中版本号后提交推送即可。
