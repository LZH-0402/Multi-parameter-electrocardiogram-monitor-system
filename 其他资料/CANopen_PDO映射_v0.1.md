# CANopen PDO 映射 v0.1

经典 CAN，标准11bit标识，500kbit/s，小端；Node-A=1，Node-B=2，Gateway-C=3。异步 TPDO transmission type255。下列 COB-ID 在项目总线上保留，A/B 不得自行改变。

| PDO | A / B COB-ID | 内容（8字节） | 周期 | Gateway RPDO |
|---|---|---|---|---|
| TPDO1 | 0x181 / 0x182 | 2110:01 32bit + :02 32bit | A20ms / B4ms | RPDO1 / RPDO6 |
| TPDO2 | 0x281 / 0x282 | 2111:01 32bit + :02 32bit | A保留不发 / B20ms | RPDO2 / RPDO7 |
| TPDO3 | 0x381 / 0x382 | 2120:01..04，各16bit | 1000ms | RPDO3 / RPDO8 |
| TPDO4 | 0x481 / 0x482 | 2100:01 epoch秒 + :02 anchor tick | 1000ms | RPDO4 / RPDO9 |
| TPDO5 | 0x681 / 0x682 | 2130:01有效性 + :02 generation | 1000ms、状态变化 | RPDO5 / RPDO10 |

TPDO5 使用显式分配的额外 COB-ID，**不是** CiA301 预定义连接集的第五路默认值。0x681/0x682 不与本系统 SDO（0x581/0x582、0x601/0x602）冲突。

映射项32bit编码为 `(index << 16) | (subindex << 8) | bit_length`。例：A TPDO1 = 0x21100120、0x21100220；Gateway 对应映射0x31100120、0x31100220。标量映射为0x21200110、0x21200210、0x21200310、0x21200410。

默认每秒波形350帧 + 时钟/标量/质量6帧 + A/B/Gateway心跳6帧，约362帧/s。按每帧保守135bit估算约49kbit/s、不到500kbit/s的10%；SDO、EMCY与重发另留余量。该值是预算，不是逻辑分析仪测量结果。

## 一致性要求

1. 每个波形 PDO 的 value 与 tick 必须来自同一次采样，不能先改时间后读新值。丢样/间隔跳变时 Gateway 放弃当前未完成批次，禁止拼成伪连续波形。
2. 每次节点重启更新 generation，先发布质量与时间锚点。Gateway 收到新 generation 后清空未完成波形；网络重连不能改变节点 generation。
3. NMT STOP/PRE-OP 不继续发送采集 PDO；500ms心跳可继续报告状态。Gateway 在 Operational 且HB有效时接收为在线。
4. 单节点HB1500ms超时只影响该节点。CAN故障不能阻塞ESP网络任务；恢复后重新接收PDO。
5. 标量快照默认同一秒生成；时钟/质量变化与标量应连续发送，最坏相差一个PDO传输周期。本版不是多PDO原子事务；高精度跨节点同步和测量事件独立时间戳留给真实节点联调时扩展。

## STM32 验证配置

当前固件在一块 F407 内运行三个独立 CANopenNode 实例，经 CAN1 硬件静默 loopback 收发。APB1=16MHz，预分频2，BS1=13，BS2=2。RX PA11 使用内部上拉以满足退出初始化的隐性电平检测，TX 不配置，不向外部 CAN 接口发送。

此测试不能证明收发器、CANH/CANL、120Ω终端或三块实板连线正确。相关原理图确认及正常模式引脚配置属于实物接入步骤，不应直接把本测试固件连接外部总线冒充生产配置。初始化问题的依据见 [ST bxCAN loopback 排查说明](https://community.st.com/stm32-mcus-60/troubleshooting-bxcan-issues-in-loop-back-mode-on-stm32-mcus-144052)。
