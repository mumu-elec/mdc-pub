/*
 * ============================================================================
 *  motor_driver.h — Motor Driver Controller 协议封装层（STM32 HAL 例程）
 * ============================================================================
 *  纯函数 + 依赖注入回调设计：本模块不依赖具体外设，
 *  所有 UART 收发通过回调由用户实现（见 motor_driver_send_uart）。
 *
 *  协议依据：common/协议规范.md
 *    - §2 文本指令（24 条，以 '\n' 结尾）
 *    - §3 二进制帧：[0xAA][CMD][LEN][DATA...][CRC8]
 *      CRC8：多项式 0x07，初值 0，计算范围 = CMD+LEN+DATA（不含 SYNC）
 *    - 所有多字节字段为小端序（LE）
 *
 *  适用：STM32F1/F4（例程按 F103C8T6 编写，F4 用法完全相同，
 *        仅需在 CubeMX 中选择对应芯片型号）。
 * ============================================================================
 */
#ifndef __MOTOR_DRIVER_H
#define __MOTOR_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------- 协议常量（规范 §3.3） ------------------------- */
#define MD_SYNC              0xAAu   /* 帧同步头 */
#define MD_CMD_MOTOR_CTRL    0x31u   /* MOTOR_CTRL：四通道批量控制（int32 LE ×4） */
#define MD_CMD_READ_PARAM    0x10u   /* READ_PARAM：读取全部配置（应答 231B config_t） */
#define MD_CMD_SUBSCRIBE     0x40u   /* SUBSCRIBE：开启状态周期上报 [interval_ms:2B LE] */
#define MD_CMD_STATUS_REPORT 0xF0u   /* STATUS_REPORT：周期状态上报（MCU 主动推送） */
#define MD_CMD_SAVE_EEPROM   0x20u   /* SAVE_EEPROM：RAM 配置写入 EEPROM */

/* ------------------------------ 函数接口 -------------------------------- */

/* CRC8：多项式 0x07，初值 0，计算范围 = CMD+LEN+DATA（不含 SYNC）。
 * 严格按规范 §3.1 参考实现移植。 */
uint8_t md_crc8(const uint8_t *d, uint16_t len);

/* 组帧：[0xAA][CMD][LEN][DATA...][CRC8]，写入 out（至少 len+4 字节），
 * 返回帧总长度。 */
uint16_t md_build_frame(uint8_t cmd, const uint8_t *data, uint8_t len, uint8_t *out);

/* 依赖注入回调：由用户实现，负责把 buf 通过 UART 发出（如 HAL_UART_Transmit）。
 * 例程中在 main_example.c 里定义。 */
void motor_driver_send_uart(uint8_t *buf, uint16_t len);

/* 发送文本指令（自动补 '\n' 结尾，规范 §2.1） */
void md_send_text(const char *cmd);

/* 发送 0x31 MOTOR_CTRL 控制帧：targets[4] 为四通道目标值（int32 LE 手动拼装）。
 * 含义取决于各通道模式：开环 = PWM(±1000)，速度 = RPM，位置 = 0.1°(±3600)。
 * 实时控制请以 30/50/100ms 间隔连续发送（规范 §3.3）。 */
void md_motor_ctrl(const int32_t targets[4]);

/* 发送 0x40 SUBSCRIBE：开启状态周期上报（最低 20ms） */
void md_subscribe(uint16_t interval_ms);

/* 发送 0x10 READ_PARAM：请求全部配置（应答由 md_on_read_param 回调解析） */
void md_read_param(void);

/* 发送 0x20 SAVE_EEPROM：RAM 配置写入 EEPROM（约 190ms） */
void md_save_eeprom(void);

/* --------------------- 接收解析（滑动窗口状态机） ------------------------ */
/* USART 每收到一个字节调用一次本函数，内部做滑动窗口帧解析，
 * 完整帧校验通过后分发到回调。请在接收中断回调中调用。 */
void md_rx_byte(uint8_t b);

/* 帧分发回调（默认弱实现，可在用户代码中重写覆盖）：
 * 收到 0xF0 STATUS_REPORT 时调用 md_on_status_report(data, len)；
 * 收到 0x10 READ_PARAM 应答时调用 md_on_read_param(cfg, len)。 */
void md_on_status_report(const uint8_t *data, uint8_t len);
void md_on_read_param(const uint8_t *cfg, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_DRIVER_H */
