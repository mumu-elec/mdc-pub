# Motor Driver Controller — 发布页
- 在线发布页：https://mumu-elec.github.io/mdc-pub

基于 STM32F401 + TB6612 的四路直流电机驱动器（Bootloader + 主固件 + Web 上位机）的**发布仓库**。

> **页面设计与维护规范（AI 系统提示词）：见 [`SPEC.md`](SPEC.md)。** 改版/发版前先读它；
> 本 README 只讲结构和工作流。

## 版本清单（releases.json）— 单一事实来源

- 发布页、固件详情页与上位机「联网版」都从根目录的 [`releases.json`](releases.json) 读取版本数据。
- 上位机联网版（`online.js`）依赖旧字段 `lines[].protocol/current/name` 与 `releases[]`（fw/date/notes/fw_bin/host）：
  **这些字段只增不改不删**，新功能一律以新增字段实现。
- 上位机联网版数据源：**当前站点优先**（按页面位置逐级向上试探 `releases.json`，固件/上位机下载跟随命中源），
  全部未命中才落到 `online.js` 顶部的 `MANIFEST_URL`（GitHub 发布页）兜底。
- 上位机形态：文件名带 `-online` 后缀 = 联网版（在线固件库 / 更新检查 / 配套引导，断网自动降级）；
  无后缀 = 完全离线版（零网络请求，功能相同）。

## 资料版本化（docs/ 快照 · 不可覆盖）

- **发布即冻结**：资料随版本发布时复制到 `docs/<线>/<资料版本>/` 快照目录，此后只读；
  修订一律产出新版本目录，禁止覆盖旧快照。
- **版本对应**：`releases.json` 的 `releases[].resources`（版本级资料）登记每个固件版本
  自己的资料；详情页优先渲染版本级，缺省回退线级 `lines[].resources`（当前版语义）。
- **首页只呈现当前线**：存档线资料不出现在首页「相关资料」，只经该线各版本详情页可达
  （JSON 里显式登记，可审计）。
- **多文件资料走 GitHub**：例程、例程库等多文件资料在 `resources` 里登记 GitHub 仓库树
  外链（浏览 / 下载均在 GitHub）；站点保留 zip 快照归档但不再作为下载入口列出。
- **无新功能不发新资料**：bug 修复版本在 `resources` 里显式沿用上一个功能版本的快照（可审计的"沿用"，不是遗漏）。
- 完整规则见主仓 `docs/资料发布规范.md`。

## 页面结构（2026-09 第六次迭代 · 版本资料分层）

- **index.html** — 发布中心首页，**不按协议线分组**：
  主区块只展示**当前维护线**——「上位机」「固件」「Bootloader」。
  区块内是**同一套扁平列表视觉**（列头 + 细线行）：
  「下载文件」（无标签行，直接列头；扩展名标签图标 + 文件名 + 说明 + 发布时间 + 下载钮）与
  「历史版本」（**含当前版本**（最新徽标）+ **全部存档线版本**（muted 弱化），新→旧；
  固件行 `[详细][⬇BIN][⬇HEX]`、上位机行 `[详细][⬇离线][⬇联网]`）；
  「相关资料」为同款列表（带标签），**只列当前线资料**，放在固件区块内；
  例程 / 例程库等多文件资料一律为 GitHub 仓库树跳转链接（下载请到 GitHub）。
- **fw/vA.D.E.html** — **每个固件发布的详情页**（壳页 + `fw-detail.js` 共享渲染器）：
  版本信息 / 更新内容 / 下载（同一列表视觉） / 手册与资料 / 固件升级方法 / 兼容性说明。
- **host/vX.Y.html** — **每个上位机发布的详情页**（壳页 + `host-detail.js` 共享渲染器）：
  版本信息 / 更新内容 / 下载 / 配套固件 / 手册与资料 / 兼容性说明。

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
| 技术手册 | v2.0 / v1.2 存档 | 当前版 `docs/technical-manual.md`；发布快照 `docs/v2/2.0/`（手册+配图+PDF+例程）、`docs/v1/1.2/`（手册+配图+PDF+AI 接口手册+协议 JSON，永不覆盖） |
| 例程 | 例程 v2.0 / mdc_lib | 多文件经 GitHub 浏览 / 下载：`docs/v2/2.0/examples/`（248B 协议口径，随固件 v1.2.0 发布）与 `mdc_lib/`（12 平台库；v2.0，2026-09-12 已对齐 248B / 协议 D=2）；zip 快照 `docs/v2/2.0/mdc_examples_v2.0.zip` 仅归档 |

> 版本规则：固件 vA.D.E（A=硬件 D=协议 E=补丁）· 上位机 vD.F（D=协议 F=修复）。上位机与固件的协议版本 D 必须一致；不匹配时联网版上位机会自动引导切换到配套版本。

## 目录结构

```
motor_driver_control/
├── index.html              # 发布中心首页（当前线区块 + 历史版本表，相关资料仅当前线）
├── SPEC.md                 # ★ 页面设计与维护规范（AI 系统提示词）
├── releases.json           # 版本清单（发布页 + 详情页 + 上位机联网版 共用的单一数据源）
├── fw-detail.js            # 固件详情页共享渲染器
├── fw/                     # 固件发布详情页（每个固件版本一个壳页）
│   ├── v1.2.0.html         #   壳页只声明版本号，内容由 fw-detail.js 从 releases.json 渲染
│   └── v1.1.*.html
├── host-detail.js          # 上位机详情页共享渲染器
├── host/                   # 上位机发布详情页（每个上位机版本一个壳页）
│   ├── v2.0.html
│   └── v1.0.html / v1.1.html
├── web/                    # 在线上位机统一目录（联网版单文件，自包含，按协议线分子目录）
│   ├── v2/index.html       #   v2 线最新上位机（index 恒等于本线最新）；v2/v2.0.html = 按版本号存历史
│   └── v1/index.html       #   v1 线最新上位机；v1/v1.1.html = 按版本号存历史
├── release/                # 发布物（上位机 online/离线 / 固件 / BL）
├── docs/                   # 对外资料（版本化：快照目录 docs/<线>/<资料版本>/，发布后冻结不改）
│   ├── technical-manual.md #   当前版技术手册（恒等于最新快照内容；唯一可被"发新版"覆盖的文件）
│   ├── v2/2.0/             #   v2.0 发布快照：手册 + image/ + 例程（examples/ 多文件 + zip 归档）
│   └── v1/1.2/             #   v1.2 发布快照：手册 + image/ + PDF + AI 接口手册 + protocol/
└── mdc_lib/                # 例程库（多平台 + 协议规范；v2.0 已对齐 248B / 协议 D=2）
```

## 更新发布物（详见 SPEC.md §5）

1. 新构建产物放入 `release/`（上位机由打包脚本产出联网版 + 离线版两个文件）。
2. 在 `releases.json` 对应线（line）的 `releases` / `host_releases` 数组**末尾追加**新版本条目。
3. **固件发版**：复制任一 `fw/v*.html` 壳页为新版本页（只改版本号），并补 `detail` / `highlights` 字段。
   **上位机发版**：复制任一 `host/v*.html` 壳页为新版本页（只改版本号），并补 `detail` 字段。
4. **资料随发**（手册/例程有更新时）：先复制定稿到 `docs/<线>/<新资料版本>/` 快照目录
   （例程放入 zip + 同内容 `examples/` 多文件目录），同一次提交里把顶层当前版文件更新为同内容；
   `releases.json` 新条目带版本级 `resources`；多文件资料登记 GitHub 树外链。
5. 提交推送，GitHub Pages 自动生效；发布页、详情页与上位机联网版即读到新版本。
