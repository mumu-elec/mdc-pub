# MDC 电机驱动板 · 固件二次开发库（firmware-dev-kit）v1.0

> 面向想在本驱动板上**开发自己固件**的开发者。本库是一个**可直接编译的最小工程骨架**：
> 只包含硬件外设初始化（时钟 / GPIO / PWM / 编码器 / 串口 / I2C / 看门狗）和一个最小演示
> 程序，**不含任何出厂固件业务逻辑**。外设参数与出厂固件一致（推荐配置），
> 引脚与外设的详细说明见配套《固件二次开发手册》。

---

## 1. 硬件前提

| 项目 | 说明 |
|------|------|
| MCU | STM32F401RCT6（LQFP64，Cortex-M4F，84MHz，256KB Flash / 64KB RAM） |
| 电机驱动 | 2 × TB6612，四路电机（连接器 A/B/C/D） |
| 编码器接口 | 4 路正交编码器（硬件 4 倍频） |
| 板载调试串口 | CH340N，Type-C（连接 USART6） |
| 调试接口 | SWD 4-Pin 排针（3V3 / SWDIO / SWCLK / GND） |
| 出厂 Bootloader | 占用 Flash 前 16KB，本库默认**保留 Bootloader 兼容** |

## 2. 目录结构

```
firmware-dev-kit/
├── motor_driver_ctrl.ioc      # CubeMX 工程文件（CubeMX 6.14+ 可直接打开改配置）
├── Makefile                   # GNU Make 构建脚本（CubeMX 风格）
├── build.bat                  # Windows 一键编译脚本
├── startup_stm32f401xc.s      # 启动文件
├── STM32F401XX_FLASH.ld       # 链接脚本（FLASH 起始 0x08004000，保留 Bootloader）
├── Core/
│   ├── Inc/                   # main.h / stm32f4xx_it.h / stm32f4xx_hal_conf.h
│   └── Src/
│       ├── main.c             # 外设初始化 + 最小 demo（你的代码从这里开始）
│       ├── stm32f4xx_it.c     # 中断服务函数
│       ├── stm32f4xx_hal_msp.c# 外设底层配置（GPIO AF / 时钟 / NVIC）
│       ├── system_stm32f4xx.c # 系统时钟初始化（ST 标准文件）
│       ├── syscalls.c / sysmem.c  # newlib 底层支持（ST 标准文件）
└── Drivers/                   # STM32F4 HAL 库 + CMSIS（按需精简自 ST 官方包）
```

## 3. 编译

依赖：`arm-none-eabi-gcc`（ARM GNU Toolchain）+ `make`（或 `mingw32-make`），并加入 PATH。

```
Windows:  build.bat          （或 build.bat clean 后重新编译）
Linux/macOS:  make -j8
```

产物在 `build/`：`mdc_devkit.elf / .hex / .bin`。

## 4. 烧录

| 方式 | 步骤 |
|------|------|
| ST-LINK（推荐调试用） | SWD 排针接 ST-LINK → `STM32CubeProgrammer` 或 `st-flash write build/mdc_devkit.bin 0x08004000`。注意地址必须是 **0x08004000**（保留 Bootloader） |
| 保留出厂 Bootloader 的上位机升级 | 本库链接基址与 Bootloader 兼容，编译出的 `.bin` 可直接用出厂上位机的"固件升级"功能刷入 |

> **不想保留 Bootloader？** 修改 `STM32F401XX_FLASH.ld`：
> `FLASH ORIGIN = 0x08000000, LENGTH = 256K`，并删除 `main.c` 中
> `SCB->VTOR = 0x08004000;` 两行（含 `__DSB/__ISB`）。此后只能用 ST-LINK 烧录。

## 5. demo 说明（main.c）

- **LED**：PA4 低电平点亮，demo 每 500ms 翻转一次；
- **编码器**：4 路编码器接口已启动，每秒经 USART6（2Mbps）打印一帧
  `ENC A= B= C= D=`（各定时器 CNT 原始值，A=TIM3 / B=TIM5 / C=TIM2 / D=TIM4）；
- **电机**：`demo_motor_a_spin()` 给出了连接器 A 开环转动的最小示例
  （方向脚 PC12/PC11 + PWM 占空比），**默认不调用**——确认机械与供电安全后，
  取消 `main()` 中相应注释即可让电机 A 缓慢转动；
- **看门狗**：IWDG 已按推荐配置启用（约 1s 超时），主循环每圈喂狗。
  若你的调试代码会长时间阻塞，请临时注释 `HAL_IWDG_Refresh` 之外的初始化段
  （或自行调整超时），避免"莫名复位"。

用 USB Type-C 连接电脑，任意串口工具以 **2000000 8N1** 打开即可看到打印。

## 6. 用 CubeMX 修改配置

1. 打开 `motor_driver_ctrl.ioc`（CubeMX 6.14+，固件包 STM32Cube FW_F4 V1.28.3）；
2. 图形化修改引脚 / 外设 / 时钟后重新生成代码——`main.c` 中 `USER CODE BEGIN/END`
   区块内的内容会被保留；
3. 两处**手写初始化**不在 CubeMX 管理范围内，重新生成后仍在 USER CODE 区：
   - `MX_USART2_UART_Init()`（PA3 控制输入口，RX-only）；
   - `MX_GPIO_Init()` 尾部的 PA2 输入下拉（旧版硬件防浮空保护，新版无影响）；
4. 若在 CubeMX 中启用 USART2 外设，删除上述手写函数避免重复初始化。

## 7. 安全与注意事项

- 电机部分为**功率电路**：首次点机请架空电机、用小占空比（如 10%~30%），并确认电源电压在板子规格范围内；
- 修改 PWM 频率（TIM1 PSC/ARR）会改变占空比分辨率与电机噪声特性，建议保持在 16~32kHz 超声频段；
- 编码器定时器 TIM3/TIM4 为 16 位计数器，高速长时间单向旋转会溢出回绕，需自行处理累计圈数；
- 本库仅含硬件初始化骨架，不包含通信协议、控制算法——请按自己的需求开发，或参考发布页《技术手册》与《例程》经上位机协议控制出厂固件。

---

配套文档：[固件二次开发手册](../firmware-dev-manual.md)（引脚分配 / 外设推荐参数 / Bootloader 布局详解）

版本：v1.0（2026-09-23）· 配套硬件：Motor Driver Controller（STM32F401 + TB6612 四路驱动板）
