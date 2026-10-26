# MPM 统一数据字典 v0.1

基线：MPM-P-SOW-002 v0.1、总合同 v0.7。2026-09-20 冻结阶段二接口；实际传感器标定和 RR 算法不在本次完成声明内。

| 名称 | 来源 | 单位 / CAN 固定点类型 | 有效位 | 类别 / CAN 周期 | 生产者 OD | MQTT stream |
|---|---|---|---|---|---|---|
| PPG | A / AFE4490 | relative，i32 / 1,000,000 | A bit0 | 波形 50Hz | 2110:01，:02 采样 tick | ppg |
| SpO2 | A / AFE4490 | %，u16 / 100 | A bit1 | 标量 1Hz | 2120:01 | spo2 |
| PR | A / AFE4490 | bpm，u16 / 100 | A bit2 | 标量 1Hz | 2120:02 | pr |
| NIBP | A / HKB-08 | mmHg，u16 / 10；SYS、DIA | A bit3 | 1Hz 快照，实际为测量事件 | 2120:03、:04 | nibp，二元素数组 |
| ECG | B / ADS1292R | mV，i32 原始数值为 µV / 1,000 | B bit0 | 波形 250Hz | 2110:01，:02 tick | ecg |
| HR | B / ADS1292R | bpm，u16 / 100 | B bit2 | 标量 1Hz | 2120:01 | hr |
| RESP_RAW | B / ADS1292R | relative，i32 / 1,000,000 | B bit1 | 波形 50Hz | 2111:01，:02 tick | resp |
| RR | P 算法，最终在 B | breaths/min，u16 / 100 | B bit3 | 预留标量 1Hz | 2120:02 | rr |
| TEMP | B / GY-641V3 | degC，i16 / 100 | B bit4 | 标量 1Hz | 2120:03 | temp |
| NODE_STATUS | A/B | state，ONLINE/OFFLINE | NMT + HB | 变化立即、云端5s快照 | 1017/HB、NMT | 节点 status |
| FAULT | A/B | code，EMCY-XXXX | EMCY 事件 | 变化事件 | EMCY / 1001 | event；历史 replay/fault |
| GATEWAY_STATUS | C | state，ONLINE/OFFLINE | MQTT连接/LWT | 5s、变化 | 本机运行状态 | Gateway status |

所有 Index 为十六进制。两个生产者使用相同私有 OD 索引，由 Node-ID 区分语义；Gateway 镜像索引见 OD 文档。

## 时间与有效性

- 2100:01 为 UTC epoch 秒；2100:02 为该整秒边界对应的节点单调毫秒 tick（u32）。波形采集时间 = epoch×1000 + u32(sample_tick−anchor_tick)。节点重启与 tick 回绕按 boot_generation 分段。
- 2130:01 是上述质量有效位掩码，bit31 为 synthetic；保留位为零。2130:02 为 boot_generation，通信/采集复位后必须递增，不允许同一会话重置序号。
- 清除有效位时发送 INVALID，绝不能用零或固定健康值冒充有效测量。RR 没有真实 RESP_RAW 和算法验证，测试固件 bit3 始终为零。
- NIBP 的质量位由 A 按真实测量时效维护；过期、测量失败或未测量必须清位。本版 1Hz PDO 是“当前有效测量快照”，不能每次重发都将一项过期结果重新认定为新测量。
- 没有可信节点时钟时不发布生理参数；Gateway 用自身时钟报告在线性，不能替节点伪造真实采集时间。
- Canonical 帧包含 timestamp、seq、session identity、node/stream、valid、synthetic、replay、样本/标量。网络重连保留采集时间、序号与节点会话。
- 波形批次 timestamp 为最后一个样本的采集时间，前面的采样时刻按 sample_rate 倒推，与现有浏览器波形轴一致；不是网络发送时刻。
- 测试源实时发送 `MOCK + synthetic=true`；真正 A/B 采集才允许 `LIVE + synthetic=false`；补传统一 `REPLAY` 且保留 synthetic。主机的 LIVE 单元夹具不会上传生产云端。

## 云端发布

波形按一秒成批（ECG 250 点，PPG/RESP 各 50 点）；CAN 标量 1Hz，Gateway 每两秒发送当前快照。状态五秒一次及在线变化立即发送。实时波形 QoS0，标量/状态/故障及全部 REPLAY QoS1；仅 status retain。

这些是阶段二经过 ESP 串口带宽验证的最小配置，不代表传感器最终带宽、临床量程或实时报警认证。后续提高采样率或降低延时须复核总线、串口、内存与云端过期阈值。
