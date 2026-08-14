# 06_config_manager — 配置读写工具（config_t 231B）

## 功能

- 按协议规范 §5 的 config_t **偏移表**定义全部字段描述（offset/size/类型/名称/说明）。
- `parse_config(231B) -> dict`：全字段解析（含受保护区头部、位域按通道解码）。
- `pack_config(dict, base) -> bytes`：按字段类型（u8/u16/u32/f32/数组）打包回 231B。
- 子命令 CLI：
  - `dump`：READ_PARAM 后打印全部可读字段（通道数组逐通道显示，位域按通道解码）；
  - `set <field[:ch]> <value>`：修改单字段 → WRITE_PARAM 写回（仅 RAM）；
  - `field <id> <value>`：演示 WRITE_FIELD 按偏移写单个字段（按类型打包）；
  - `save`：SAVE_EEPROM 持久化。
- 交互模式：无子命令进入，菜单选择 dump / set / save / exit。

## 字段通道语法

| 语法 | 示例 | 含义 |
|------|------|------|
| `字段名:通道` | `speed_olim:1 800` | 修改通道 1 的 speed_olim |
| `字段名`（数组） | `encoder_cpr 500 500 500 500` | 一次赋 4 个通道 |
| 每通道 4 参数 | `speed_ctrl_params:1 0.5 0.02 0.01 0.5` | 通道 1 的 kp/ki/kd/ilim |
| `mode`（别名） | `mode:1 speed` / `mode speed speed speed speed` | control_mode，支持 open/speed/pos 单词 |

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller（固件 v1.2.0+，协议版本 D=2），USB Type-C 线 |
| 驱动 | CH340N 虚拟串口驱动 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖安装

```bash
pip install -r requirements.txt
```

## 运行方法

```bash
# 读取并打印全部配置
python config_manager.py dump

# 修改通道1 编码器线数并写回（仅 RAM）
python config_manager.py --port COM5 set encoder_cpr:1 500

# 修改通道1 为速度模式（支持 open/speed/pos 单词）
python config_manager.py set mode:1 speed

# 修改速度环输出限幅
python config_manager.py set speed_olim:1 800

# 演示 WRITE_FIELD 按偏移写单字段（标量字段；支持字段名或数字偏移）
python config_manager.py field cmd_timeout_ms 500
python config_manager.py field 15 500

# 持久化到 EEPROM
python config_manager.py save

# 交互模式
python config_manager.py
```

> ⚠️ 写入仅修改 RAM（`OK (RAM only)`），重启后恢复；需执行 `save` 持久化。
> 修改 `baud_rate` / `comm_flags`（protocol/sbus_inv）会触发固件重新初始化 USART2（本工具走 USB 口，不受影响）。

## 代码结构

| 函数/类 | 作用 |
|---------|------|
| `FIELD_TABLE` | 协议规范 §5 偏移表（name/offset/fmt/count/desc） |
| `parse_config()` | 231B → dict（字段 + `_header` 受保护区 + `_decoded` 位域解码） |
| `pack_config()` | dict → 231B bytes（u8/u16/u32/f32/数组按类型打包） |
| `apply_set()` | 在 raw 上应用 `set` 修改（2bit/4bit 打包位域、数组、ctrl_params） |
| `Driver` | 最小二进制协议客户端（READ_PARAM/WRITE_PARAM/WRITE_FIELD/SAVE_EEPROM） |
| `print_dump()` / `cmd_set()` / `cmd_field()` / `cmd_save()` | 各子命令实现 |
| `interactive()` | 交互菜单 |

## 用到的协议命令（二进制）

`0x10 READ_PARAM`、`0x11 WRITE_PARAM`、`0x12 WRITE_FIELD`（`[field_id:2B LE][value]`，offset<12 受保护区拒绝）、`0x20 SAVE_EEPROM`。

## 字段偏移速查（协议规范 §5）

`baud_rate@11` `cmd_timeout_ms@15` `comm_flags@17` `control_mode@18` `motor_invert@19` `encoder_cpr@20` `speed_period_ms@28` `speed_pid_type@36` `speed_olim@38` `speed_ctrl_params@46` `pos_period_ms@110` `pos_pid_type@118` `pos_ctrl_params@120` `pos_olim@184` `pos_angle_cpr@200` `speed_filter_type@208` `speed_filter_window@210` `sbus_channel_pack@214` `rc_dir_ch@216` `rc_map_mode@218` `sbus_param@219` `sbus_range_min@227` `sbus_range_max@229`

## 常见问题

| 现象 | 处理 |
|------|------|
| `未知字段` | 用 `dump` 查看字段名；别名仅 `mode`、`cpr` |
| `值超出范围` | 按类型范围输入：u8 0~255 / u16 0~65535 / u32 0~4294967295 |
| `field` 报“数组字段” | 数组字段请用 `set`（可 `字段:通道` 指定单通道） |
| `field` 报“受保护区” | offset<12 的 magic/版本/CRC 不可写（规范 §3.3），请用 `set` + WRITE_PARAM（固件自动还原保护字段） |
| 修改后重启恢复 | 执行 `save` 持久化 |
| WRITE_PARAM 后 USART2 失效 | 修改了 `baud_rate`/`comm_flags`，固件会重配 USART2 并自动持久化，属正常行为 |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
