# Motor Driver Controller 发布页

基于 STM32F401 + TB6612 的四路直流电机驱动器（Bootloader + 主固件 + Web 上位机）的官方发布仓库。

- 在线发布页：https://mumu-elec.github.io/mdc-pub

## 版本线

| 线 | 协议 | 固件 | 上位机 | 状态 |
|----|------|------|--------|------|
| v2 · 底盘 / 单电机一体 | D=2 | v1.2.0 | v2.0 | 当前维护 |
| v1 · 存档 | D=1 | v1.1.0 · v1.1.1 · v1.1.2 | v1.0 · v1.1 | 存档 |

版本规则：固件 vA.D.E（A=硬件、D=协议、E=修复），上位机 vD.F（D=协议、F=修复）。
上位机与固件的协议版本 D 必须一致才能通信；不匹配时联网版上位机会自动引导切换到配套版本。

## 这里有

- **在线上位机**：[web/v2/](https://mumu-elec.github.io/mdc-pub/web/v2/)（v2 线最新，浏览器即开即用，需 Chrome / Edge）；v1 线在 [web/v1/](https://mumu-elec.github.io/mdc-pub/web/v1/)，历史版本按文件名存于同目录。
- **上位机离线包**（`release/`）：联网版文件名带 `-online` 后缀，含在线固件库与检查更新，断网自动降级；无后缀为完全离线版，零网络请求，功能相同。
- **固件**（`release/motor_driver_ctrl-v*.bin/.hex`）：每个版本在 `fw/` 有详情页——更新内容、下载、配套资料、升级方法。
- **Bootloader**（`release/bootloader-v1.0.bin`）：仅产线首次烧录用（ST-LINK 写入）；日常固件升级在上位机「固件升级」内完成，无需单独刷写。
- **技术手册**（`docs/`）：当前版 v2.0（`docs/technical-manual.md`，附 PDF 与 Python / Arduino 例程）；v1.2 存档于 `docs/v1/1.2/`（含 AI 接口手册与协议 JSON）。
- **调用库**（`mdc_lib/`）：6 类 12 平台（Python / C++ / STM32 / ESP32 / RP2040 / ESP8266 / 51）通用调用库与协议规范，248B / D=2 口径。

## 目录速览

| 路径 | 内容 |
|------|------|
| `index.html` + `releases.json` | 发布中心首页及其版本数据 |
| `fw/` · `host/` | 各固件 / 上位机版本的发布详情页 |
| `release/` | 全部下载产物（固件 BIN/HEX、上位机离线与联网版、Bootloader） |
| `web/` | 在线上位机（v2 当前线 / v1 存档线） |
| `docs/` | 技术手册与例程（按版本存档） |
| `mdc_lib/` | 多平台调用库与协议规范 |
