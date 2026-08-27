# 周期状态上报（对应固件 0xF0 STATUS_REPORT，常规 56B / 扩展 72B 均可解析）
std_msgs/Header header
int32[4] enc       # 编码器累计脉冲
float32[4] tgt     # 当前目标值
int32[4] rpm       # 滤波后实时转速
uint32 sbus_frame_cnt
uint32 sbus_ok_cnt
