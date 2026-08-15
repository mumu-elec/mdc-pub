# 06_config_manager — 配置读写工具（config_t 231B）

## 功能

- 基于 mdc_lib 的完整配置读写演示：帧打包/解析、config_t 解析/打包全部调用 mdc_lib。
- `dump`：`md_bin_read_param()` 发送 → 接收 231B 应答 → `md_parse_config` 解析为 dict → 按分组打印全部字段（通道数组逐通道显示）。
- `set <field[:ch]> <value>`：读当前配置 → 修改 dict → `md_pack_config` 打包 + `md_bin_write_param` 写回（仅 RAM）。
- `field <名称|偏移> <value>`：演示 `md_bin_write_field` 按偏移写单个标量字段。
- `save`：`md_bin_save()` 持久化到 EEPROM。
- 交互模式：无子命令进入，菜单选择 dump / set / save / exit。

## 字段通道语法

字段名 = mdc_lib `md_parse_config` 返回的 dict 键名（见 [`mdc_lib/API.md`](../../../mdc_lib/API.md) §6.5）：

| 语法 | 示例 | 含义 |
|------|------|------|
| `字段名:通道` | `speed_olim:1 800` | 修改通道 1 的 speed_olim |
| `字段名`（数组） | `encoder_cpr 500 500 500 500` | 一次赋 4 个通道 |
| 每通道 PID 组 | `speed_pid:1 0.5 0.02 0.01 0.5` | 通道 1 的 kp/ki/kd/ilim（`pos_pid` 同理） |
| `mode`（别名） | `mode:1 speed` / `mode speed speed speed speed` | control_mode，支持 open/speed/pos 单词 |
| `cpr`（别名） | `cpr:1 500` | encoder_cpr |

> 位域字段（control_mode / motor_invert / 各 pid_type / filter_type / sbus_channel / rc_dir_ch / rc_map_mode / rc_dir_en 等）由 mdc_lib 按通道解码/打包，`set` 直接按通道赋 API 值即可（如 `sbus_channel:1 3` 表示遥控 CH3）。

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller（固件 v1.2.0+，协议版本 D=2），USB Type-C 数据线 |
| 驱动 | CH340N 虚拟串口驱动 |
| 接线 | USB 线连接设备与电脑；设备需上电 |
| 系统 | Windows / Linux / macOS 均可 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖与 mdc_lib

```bash
pip install pyserial        # 或 pip install -r requirements.txt
```

- **mdc_lib**：0x10/0x11/0x12/0x20 命令帧的打包、ACK/应答解析、config_t 解析（`md_parse_config`）与打包（`md_pack_config`）统一由通用调用库完成，本脚本只负责串口收发与字段编辑逻辑。
  正式工程把 `mdc_lib/python/mdc_lib.py` 复制到项目目录即可；本仓库内直接运行时代码已自动加载（见文件顶部）。

## mdc_lib 调用指南

本例程用到的 mdc_lib API（详见 [`mdc_lib/API.md`](../../../mdc_lib/API.md) §5/§6）：

| API | 返回 | 说明 |
|-----|------|------|
| `md_bin_read_param()` | 整帧 bytes | 打包 0x10 READ_PARAM |
| `md_parse_config(raw231)` | `dict` | 231B config_t → 字段字典（键名见 API.md §6.5） |
| `md_pack_config(dict)` | 231B bytes | 字段字典 → config_t（缺省取中性值；受保护区置 0） |
| `md_bin_write_param(cfg)` | 整帧 bytes | 打包 0x11 WRITE_PARAM（cfg 可为 dict 或 231B bytes） |
| `md_bin_write_field(field_id, value, value_len)` | 整帧 bytes | 打包 0x12 WRITE_FIELD（`[field_id:2B LE][value]`，offset<12 受保护区拒绝） |
| `md_bin_save()` | 整帧 bytes | 打包 0x20 SAVE_EEPROM |
| `MDParser()` / `parser.feed(byte)` | `(cmd, payload)` 或 `None` | 流式解析应答帧 |
| `md_parse_ack(payload)` | `Ack(cmd, err)` | 解析 ACK（err=0 成功） |

实际调用示例（串口收发由用户侧实现）：

```python
import mdc_lib, serial

ser = serial.Serial("COM5", 2000000)
parser = mdc_lib.MDParser()

ser.write(mdc_lib.md_bin_read_param())     # ① 读配置
raw = ...                                  # ② 收 231B 应答（本例程封装为 _wait_frame）
cfg = mdc_lib.md_parse_config(raw)         # ③ 解析为 dict
cfg["encoder_cpr"][0] = 500                # ④ 修改字段
ser.write(mdc_lib.md_bin_write_param(cfg)) # ⑤ dict 自动打包回写（RAM only）
ser.write(mdc_lib.md_bin_save())           # ⑥ 持久化
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

# 设置通道1 速度环 PID（kp ki kd ilim）
python config_manager.py set speed_pid:1 0.5 0.02 0.01 0.5

# 演示 md_bin_write_field 按偏移写单字段（标量字段；支持字段名或数字偏移）
python config_manager.py field cmd_timeout_ms 500
python config_manager.py field 15 500

# 持久化到 EEPROM
python config_manager.py save

# 交互模式
python config_manager.py
```

> ⚠️ 写入仅修改 RAM（`OK (RAM only)`），重启后恢复；需执行 `save` 持久化。
> 修改 `baud_rate` / `protocol` / `sbus_inv` 会触发固件重新初始化 USART2（本工具走 USB 口，不受影响）。

## 代码结构

| 函数/类 | 作用 |
|---------|------|
| `FIELD_TABLE` | 字段表（键名 = mdc_lib dict 键名；含类型/通道数/偏移/说明） |
| `Driver` | 串口收发层：`md_bin_*` 打包发送，`MDParser` 收帧，`md_parse_ack` 校验 |
| `apply_set()` | 在 dict 上应用 `set` 修改（PID 组 / control_mode 单词 / 通道数组 / 标量） |
| `print_dump()` | READ_PARAM → `md_parse_config` → 分组打印 |
| `cmd_set()` / `cmd_field()` / `cmd_save()` | 各子命令实现（全部经 mdc_lib） |
| `interactive()` | 交互菜单 |

## 常见问题

| 现象 | 处理 |
|------|------|
| `未知字段` | 用 `dump` 查看字段名（= mdc_lib dict 键名）；别名仅 `mode`、`cpr`、`speed_pid`、`pos_pid` |
| `值超出范围` | 按类型范围输入：u8 0~255 / u16 0~65535 / u32 0~4294967295；控制模式 0~2；遥控通道 1~16 |
| `field` 报“未知字段/偏移不支持” | field 仅支持标量字段：`baud_rate`(11)、`cmd_timeout_ms`(15)、`sbus_range_min`(227)、`sbus_range_max`(229)；数组/位域字段请用 `set` |
| `field` 报“受保护区” | offset<12 的 magic/版本/CRC 不可写（规范 §3.3），请用 `set` + WRITE_PARAM（固件自动还原保护字段） |
| 修改后重启恢复 | 执行 `save` 持久化 |
| WRITE_PARAM 后 USART2 失效 | 修改了 `baud_rate`/`protocol`/`sbus_inv`，固件会重配 USART2 并自动持久化，属正常行为 |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
