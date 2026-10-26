# 监护仪 Node-A CANopen 协议扩展说明（上报 Gateway-C）

> 对应《NodeA_未完成任务清单》§2.1。
> 本文档说明：为把双屏监护仪的最新生理参数经 CAN 总线上报给 Gateway-C，
> 在 **CANopenNode v4 默认 DS-301 对象字典**之外**新增的私有对象字典（制造商专用区 0x2100–0x2130 + 0x2110/0x2111）**，
> 以及对应的 TPDO 分配、生产者映射、Gateway 侧需镜像的内容，并列出本次相对基线的全部修改。
>
> 设计决策（已与确认）：首版**同时上报标量 + 状态 + PPG 波形**；采用**扩展私有 OD**方式，
> 不改动 DS-301 标准通信对象，因此 Gateway 侧只需镜像私有区即可解析，标准 NMT/SDO/EMCY/Heartbeat 不受影响。
> 私有区实现严格对齐三份 v0.1 规范（《NodeA 私有对象字典 v0.1》《NodeA 通信参数 v0.1》《MPM 统一数据字典 v0.1》），
> 详见 §11 的 D1–D11 偏差闭环说明。

---

## 1. 通信参数（固定）

| 项 | 值 |
|----|----|
| 物理层 | STM32F407 内部 bxCAN，PA11(RX)/PA12(TX)，AF9 |
| 波特率 | 500 kbps（PCLK1=42MHz，Prescaler=6，BS1=11tq，BS2=2tq，SJW=1tq，采样点≈85.7%） |
| 节点 ID | **Node-A = 1** |
| 启动行为 | `CO_NMT_STARTUP_TO_OPERATIONAL`：上电即自主进入 OPERATIONAL，无需 NMT 主机 |
| 协议栈 | CANopenNode v4，`CO_USE_GLOBALS` 静态对象模式（免 heap，适配裸机） |

COB-ID 分配规则：`TPDO<n> = 0x180 + NodeID`，故 Node-A 的 5 路 TPDO 为
**0x181 / 0x281 / 0x381 / 0x481 / 0x681**（NodeID=1）。
> 注：旧文档曾误列 0x581 为第 5 路（实为 0x681），并已把 0x581 用作第 5 路状态 TPDO。
> 经 D2 重排后，第 5 路 TPDO 的 COB-ID 固定为 **0x681**（映射 0x2130 有效性），0x581 不再使用。

---

## 2. 对象字典扩展（私有区 0x2100–0x2130 + 0x2110/0x2111）

以下索引**均为本次新增**，基线 CANopenNode 默认 DS-301 不含。各子对象均已置
`ODA_TPDO` 属性（可映射到发送 PDO）。**已删除 0x2140/0x2150/0x2160**（D2/D3，规范未定义且被 2130 位图与 2110 波形取代）。

| 索引 | 子索引 | 名称 | 类型 | 可映射TPDO | 含义 |
|------|--------|------|------|-----------|------|
| 0x2100 | 0 | highestSub_indexSupported | uint8 | 否 | =2 |
| 0x2100 | 1 | epoch | uint32 | 是 | **UTC epoch 秒**（1970-01-01 起，由软 RTC 日历换算；D7） |
| 0x2100 | 2 | tick | uint32 | 是 | 节点单调毫秒节拍（= uwTick），与 0x2110:02 同域，供 Gateway 时间对齐 |
| 0x2110 | 0 | highestSub_indexSupported | uint8 | 否 | =2 |
| 0x2110 | 1 | ir_wave | int32 | 是 | PPG(IR) 波形采样（AFE4490 LED1VAL），按 ADC 满量程 2²² 归一为 **relative i32 / 1,000,000**（D8）；每 UNO 帧 1 点 |
| 0x2110 | 2 | sample_tick | uint32 | 是 | 采样时刻 uwTick(ms)，与 0x2100:02 同域，配对 0x2110:01 做波形时间对齐 |
| 0x2111 | — | （预留，MPM 规范保留） | — | 否 | Node-A 未填充，保留给 MPM 后续扩展 |
| 0x2120 | 0 | highestSub_indexSupported | uint8 | 否 | =4 |
| 0x2120 | 1 | SpO2 | uint16 | 是 | 血氧饱和度，定点单位 **u16 / 100**（%，D8） |
| 0x2120 | 2 | PR | uint16 | 是 | 脉率 bpm，定点单位 **u16 / 100**（D8） |
| 0x2120 | 3 | NIBP_SYS | uint16 | 是 | 收缩压 mmHg，定点单位 **u16 / 10**（D8） |
| 0x2120 | 4 | NIBP_DIA | uint16 | 是 | 舒张压 mmHg，定点单位 **u16 / 10**（D8） |
| 0x2130 | 0 | highestSub_indexSupported | uint8 | 否 | =2 |
| 0x2130 | 1 | data_valid_flags | uint32 | 是 | 各通道数据有效性位掩码（见 §5；bit31=synthetic，D9） |
| 0x2130 | 2 | boot_generation | uint32 | 是 | 启动代次，每次上电/通信重置自增，Gateway 据此判重启（D9） |

> 定点单位（D8）说明：SpO2/PR 原始为整数（int16），×100 后仍在 u16 范围；NIBP 为 uint16，×10 后仍在 u16 范围；
> PPG 原始为 AFE4490 22-bit ADC 计数，÷2²²（4194304）×1,000,000 得 relative int32，满量程≈1,000,000，对齐 MPM 的 2110:01 固定点类型。
> 屏端显示仍用裸计数 + 自适应归一化，**与上报解耦**，不受影响。

---

## 3. TPDO 分配（Node-A → Gateway-C）

5 路 TPDO，`transmissionType = 0xFF`（事件驱动 + 周期）。
TPDO1（波形）`eventTimer = 8ms`（=125 样点/秒，与 UNO 实际 PPG 采样率对齐，D6 保持 125Hz，见 §10）；
TPDO3/TPDO4/TPDO5（标量/状态/时钟/有效性）`eventTimer = 1000ms`（=1Hz，D10）；
TPDO2 映射为空、eventTimer=0，**保留为禁用**（对应 MPM 预留的 0x2111，Node-A 不发送）：

| TPDO | COB-ID | 通信参数(0x18xx) | 映射参数(0x1Axx) | 映射对象（来自 §2） |
|------|--------|------------------|------------------|---------------------|
| TPDO1 (0x1800) | **0x181** | eventTimer=8ms | 1A00 → 0x2110 | ir_wave(int32) + sample_tick(uint32)，共 8 字节/帧 |
| TPDO2 (0x1801) | **0x281** | eventTimer=0（禁用） | 1A01 → 空 | 预留（0x2111），Node-A 不发 |
| TPDO3 (0x1802) | **0x381** | eventTimer=1000ms | 1A02 → 0x2120 | SpO2(u16)+PR(u16)+NIBP_SYS(u16)+NIBP_DIA(u16)，共 8 字节/帧 |
| TPDO4 (0x1803) | **0x481** | eventTimer=1000ms | 1A03 → 0x2100 | epoch(uint32)+tick(uint32)，共 8 字节/帧 |
| TPDO5 (0x1804) | **0x681** | eventTimer=1000ms | 1A04 → 0x2130 | data_valid_flags(uint32)+boot_generation(uint32)，共 8 字节/帧 |

> 总线负载：波形 TPDO1 每 8ms 一帧（125 帧/s × 8B ≈ 1 kB/s 数据段），标量/状态 TPDO3–5 各 1 帧/s；
> 500 kbps 下余量充足。若 UNO 固件改动使采样率变化，仅改 `OD.c` 中 `x1800_TPDOCommunicationParameter.eventTimer` 即可（如 250Hz→4ms、500Hz→2ms）。

---

## 4. 生产者映射（应用层 → OD）

刷新在 `canopen_app_process()` 内每轮主循环先调用 `canopen_app_produce()` 写入 OD RAM，
随后 `CO_process_TPDO()` 发送，确保当周期报文携带最新值。

| OD 字段 | 数据来源 | 说明 |
|---------|----------|------|
| 0x2120.SpO2 | `g_meas.spo2` | 定点 ×100（D8） |
| 0x2120.PR | `g_meas.pulse_rate` | 定点 ×100（D8） |
| 0x2120.NIBP_SYS/DIA | `bp_last_get().sys/dia` | 仅 `bp_last.valid` 时写定点 ×10（D8），否则写 0 |
| 0x2110.ir_wave | `UNO_WaveTakeCAN()` | PPG 波形 IR 采样，按 2²² 归一为 relative i32/1,000,000（D8）；CAN 专用环，独立于屏显示环 |
| 0x2110.sample_tick | `UNO_WaveTakeCAN()` 回带 | 采样时刻 uwTick |
| 0x2100.epoch | `spo2_rtc_get_epoch()` | 软 RTC 日历换算的 UTC epoch 秒（D7），不再写 uwTick/1000 |
| 0x2100.tick | `uwTick` | 节点单调毫秒节拍 |
| 0x2130.data_valid_flags | 见 §5 | 按分区位图计算（D9） |
| 0x2130.boot_generation | `s_boot_generation` | 每次 init/通信重置自增（D9） |

> 0x2140(SQI/探头) 与 0x2150(NIBP 脉率/状态机) 已在 D2/D3 删除：探头状态由 2130:01 的 `DV_PROBE_ON` 位承载，
> NIBP 状态由 `DV_NIBP_*` 位承载；连续 SQI 在规范中无对应字段，屏端仍用 `g_meas.signal_quality` 显示，不再单独上送。

---

## 5. data_valid_flags 位定义（0x2130.sub1，u32 掩码）

分区兼容方案（D9）：**bit0–3 严格对齐 MPM 统一数据字典 v0.1 的「有效位」列**（PPG/SpO2/PR/NIBP），
**bit31 = synthetic**（真实 A/B 数据恒 0，与 MPM 兼容）；**bit4–7 为 Node-A 厂商扩展**（探头/NIBP 状态/波形可取），不与 MPM 区冲突。

| 位 | 名称 | 置位条件 | 分区 |
|----|------|----------|------|
| bit0 | PPG 波形信号有效 | `g_meas.probe_state != 0`（有探头即有 PPG 源） | MPM A bit0 |
| bit1 | SpO2 有效 | `g_meas.spo2 > 0 && g_meas.result_valid != 0` | MPM A bit1 |
| bit2 | PR 有效 | `g_meas.pulse_rate > 0` | MPM A bit2 |
| bit3 | NIBP 有效测量快照 | 见下方「NIBP 时效」（D11） | MPM A bit3 |
| bit4 | 探头已接入 | `g_meas.probe_state != 0` | Node-A 扩展 |
| bit5 | NIBP 测量中 | `bp_state == ST_MEASURING` | Node-A 扩展 |
| bit6 | NIBP 停止中 | `bp_state == ST_STOPPING` | Node-A 扩展 |
| bit7 | PPG 波形有采样可取 | `UNO_WavePendingCAN() > 0`（CAN 波形环有积压） | Node-A 扩展 |
| bit31 | synthetic（合成/占位） | 恒 0（真实数据） | MPM 兼容 |

**NIBP 时效语义（D11）**：`bit3` 仅在「结果为有效 **且** 当前不在新一轮测量/停止中 **且** 距上次成功测量 ≤ `NIBP_STALE_MS`(=1,800,000ms=30min)」时置位；
否则清位（涵盖：从未测量、测量失败、旧结果被新一轮测量取代、结果超时陈旧四种情况）。`uwTick` 32 位回绕已处理。

> Gateway 解析各通道前应先查对应有效位；未置位表示该项当前无有效测量值（如血压未测过/已超时）。
> `boot_generation` 递增即表示 Node-A 重启，Gateway 应据此刷新全部通道状态。

---

## 6. Gateway-C 侧镜像要求

为使 Gateway-C 能接收并解析上述数据，需在 Gateway-C 的对象字典与 PDO 配置中**镜像**以下内容：

1. **建立相同的私有 OD 条目** 0x2100/0x2110/0x2120/0x2130（0x2111 预留即可，子索引、数据类型、长度相同），子对象置 `ODA_RPDO` 可映射属性。
2. **建立 5 路 RPDO**，COB-ID 分别 = `0x181 / 0x281 / 0x381 / 0x481 / 0x681`（与 Node-A TPDO 一一对应），
   映射对象同 §3，使 Gateway-C 能接收并落盘/转发。其中 0x181 为 PPG 波形流（每帧 ir_wave + sample_tick），建议按 sample_tick 连续拼接还原波形；0x281 为预留、可不接。
3. **解析 data_valid_flags**（§5）判断各通道有效性；监测 `boot_generation` 递增以识别 Node-A 重启。
4. 标准对象（NMT/Heartbeat/EMCY/SDO/Time/SYNC）沿用 DS-301，无需为上报额外改动。

---

## 7. 相对基线/原合并工程的修改清单

本次实现相对「CANopenNode 默认栈 + 原双屏合并工程」做了以下新增与修正：

1. **新增应用封装层** `HARDWARE/CANopen/canopen_app.c` / `canopen_app.h`
   （协议栈初始化、`CO_process` 周期处理、生产者写 OD、CAN 中断入口封装、EMCY 封装）。
2. **对象字典生成器 `gen_od.py` 修正**（已在 `OD.c` 落地）：扩展对象
   （0x1804 / 0x1A04 / 0x2100–0x2130）初始化器每个顶层成员结束处**缺逗号**，
   且 `ODObjs` 结构体末尾**缺 `};`**；已在 OD.c 中补正。
3. **bxCAN 驱动 `CO_driver.c` 移植修正**：
   - 接收/发送中断使能宏由 HAL 风格 `CAN_IT_RX0 / CAN_IT_TX` 改为 SPL 风格
     `CAN_IT_FMP0 / CAN_IT_TME`（STM32F4 标准库无前者）；
   - `CAN_Cmd(CAN1, DISABLE)` 在 STM32F4 SPL 中**不存在**，改为
     `RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, DISABLE)` 关闭外设时钟；
   - `CO_CANinit` 第三参 `bitRate` 为 uint16，本工程被驱动忽略（bxCAN 内部硬编码 500k），
     故 `canopen_app.h` 中 `CANOPEN_BITRATE` 取 `500u` 占位（避免 500000 截断警告）。
4. **LSS slave 编译**：因 `305/CO_LSS.h` 默认无条件定义了 `CO_CONFIG_LSS`（标准 CANopenNode 行为），
   LSS slave 实际被启用，故将 `305/CO_LSSslave.c` 加入工程编译，
   否则链接报 `CO_LSSslave_init` / `CO_LSSslave_process` 未定义。
5. **血压类型对外化** `app_bp.h` / `app_bp.c`：将 `bp_state_t` / `bp_last_t` 提升为对外类型，
   新增 `bp_last_get()` / `bp_state_get()` 访问器，供生产者只读读取最近血压结果；D11 另在 `bp_last_t` 增 `valid_tick`（成功测量时的 uwTick）。
6. **工程集成**：
   - `main.c`：外设初始化后调用 `canopen_app_init()`；主循环内调用 `canopen_app_process()`；
   - `stm32f4xx_it.c`：CAN1_RX0 / CAN1_TX / CAN1_SCE 三个向量统一转调 `canopen_app_CAN_interrupt()`；
   - `.uvprojx`：新增 `CANopen` 分组（含 `canopen_app.c` / `CO_driver.c` / `OD.c` / `CANopen.c` /
     各 301/303/305 模块 `.c`）与 include 路径 `..\HARDWARE\CANopen`。

### PPG 波形上送（本期实现，走 TPDO1 0x181）
7. **UNO 波形双环解耦** `uno.h` / `uno.c`：在 USART6 中断里把每个 IR 采样**同时**推入屏显示环 `uno_wave`
   与新增的 CAN 专用环 `uno_wave_can`（容量同为 256，2 的幂）。两消费者各自独立取用
   （`on_uno_frame()` 取屏环、`canopen_app_produce()` 取 CAN 环），避免单消费者环被两次
   `UNO_WaveTake` 互相掏空。新增 `UNO_WaveTakeCAN()` / `UNO_WavePendingCAN()` / `UNO_WaveClearCAN()`
   与 `g_uno_wave_can_drop` 诊断计数；`UNO_Reset()` 同步复位 CAN 环。
8. **TPDO1 + 波形对象字典** `OD.h` / `OD.c`：新增 `x1800_TPDOCommunicationParameter`（COB-ID=**0x181**、`transmissionType=0xFF`、`eventTimer=8ms`）
   与 `x1A00_TPDOMappingParameter`（映射 `0x2110.sub1` int32/4B + `0x2110.sub2` uint32/4B，共 8B/帧）；
   `OD_RAM_t` 新增 `x2110_ppg_waveform.ir_wave`(int32) 与 `sample_tick`(uint32)；同步新增 `OD_ENTRY_H2110`（list 槽 36）。
9. **波形生产者** `canopen_app.c`：`canopen_app_produce()` 内从 CAN 环取一个 (IR 采样, 采样 tick) 配对写入
   `OD_RAM.x2110_ppg_waveform` 并 `CO_TPDOsendRequest(&g_CO->TPDO[0])` 请求发送；
   新增 `DV_WAVE_VALID`(bit7) 在 CAN 波形环有积压时置位。

---

## 8. 编译与验证状态

- **工具链**：Keil MDK V5.14 / armcc AC5（ARM Compiler 5）。
- **整工程构建（UV4 -r，含 PPG 波形上送）**：**0 Error(s), 0 Warning(s)**。
- **程序尺寸**：Code=44316 B，RO-data=3012 B，RW-data=1480 B，ZI-data=15552 B
  （Code + RO = 47328 B ≈ 46.2 KB；RW + ZI = 17032 B ≈ 16.6 KB；ROM = 48808 B ≈ 47.7 KB）。
- **运行时回环/总线验证**：需实际硬件（Node-A 的 PA11/PA12 接 CAN 收发器或 CAN 分析仪）；
  在 Gateway-C 侧按 §6 配置 5 路 RPDO 后，应能在 8ms（波形）与 1s（标量/状态/时钟/有效性）节奏收到对应 TPDO 帧。
  **本开发环境无 CAN 硬件，未做在线回环验证**，仅完成协议栈编译自洽。

---

## 9. 波形扩展已落地（本期实现，走 TPDO1 0x181）

PPG(IR) 波形已通过 **TPDO1（COB-ID=0x181）** 上送（映射 0x2110: ir_wave + sample_tick），详见 §2(0x2110)、§3(TPDO1)、§4、§5(bit7)、§7(7–9)。
要点与可调项：
- **波形源**：UNO(AFE4490) 每帧 1 个 IR 原始 ADC 采样，经 CAN 专用环 `uno_wave_can` 上送；与屏显示完全解耦。
- **速率（已对齐实测）**：TPDO1 `eventTimer=8ms` ⇒ 上限 125 样点/秒，与 UNO(AFE4490) 固件实际 PPG 采样率一致（D6 保持 125Hz，见 §10）。
- **缩放回放**：Gateway 侧按 `ir_wave`（relative i32/1,000,000，满量程≈1,000,000）结合 `sample_tick` 还原波形与时间轴；`data_valid_flags.bit7` 指示流是否在线。
- 当前仅 IR 通道上送（RED 通道在原工程未做连续波形环，仅 `g_meas.red_raw` 单点）；如后续需 RED 波形，在 `uno.c` 中断里同步入第三个环并在 0x2110 增 sub3 映射即可，不影响现有结构。

> 说明：本项目硬件仅含 SpO2(AFE4490 PPG) + NIBP(血压) 两路，无 ECG 通道，故波形扩展仅覆盖血氧 PPG。

---

## 10. PPG 采样率来源（实测确认，决定 TPDO1 eventTimer）

TPDO1 的 `eventTimer` 取值依据 **UNO(AFE4490) 固件实测采样率**，而非猜测：

- 固件路径：`C:\Users\ASUS\Documents\Arduino\libraries\AFE4490`
- `examples/Example-1-Raw-PPG-plot-openview/Example-1-Raw-PPG-plot-openview.ino`：
  - `Serial.begin(57600)`（与 F407 的 USART6 @57600 一致）；
  - `loop()` 内 `delay(8)` 后无条件调用 `send_data_serial_port()` 发一帧
    （帧头 5B + DATA_LEN=10B + 帧尾 2B = 17B，含 `IR_data`(4B)+`RED_data`(4B)+spo2(1B)+hr(1B)），
    即 **帧率 = 1000/8 ≈ 125 Hz**，与 F407 `UNO_ParseByte` 解析的 `0A FA 0A 00 02 + 10B + 00 0B` 帧一一对应。
- `src/S_hr_algorithm.cpp`：`freq = 125;` —— HR 算法按 125 Hz 输入设计。
- `src/S_afe44xx.cpp`：注释 `dec == 5 // fill SpO2 buffer every 5 raw samples (~25Hz)`，
  即 原始 125Hz 经 5 抽 1 → 25Hz SpO2 缓冲，反推原始采样 **125 Hz**。

⇒ **结论：PPG（IR 波形）源速率 = 125 Hz**。据此 TPDO1 `eventTimer = 8ms`（=125 样点/秒），
既匹配源速率、又留有余量（CAN 专用环 256 深，血压屏 9600bps 突发期间最多积压约 44 点，远未溢出）。
若 UNO 固件改动使采样率变化，仅改 `OD.c` 中 `x1800_TPDOCommunicationParameter.eventTimer` 即可
（如 500Hz→2ms、250Hz→4ms）。

---

## 11. 相对 v0.1 规范的偏差闭环（D1–D11）

下表汇总三份 v0.1 规范与原始实现的偏差，以及本工程已落地的修正（均编译 0E/0W）：

| 编号 | 偏差 | 修正（已落地） | 对应代码 |
|------|------|----------------|----------|
| D1 | 私有 OD 未对齐规范范围（含 0x2140/2150/2160、缺 0x2110/2111） | 私有区收敛为 0x2100/2110/2120/2130（0x2111 预留），删 0x2140/2150/2160 | OD.h / OD.c |
| D2 | TPDO 映射错位（时钟/标量/状态混排） | 重排为 5 路：TPDO1→2110 波形、TPDO2→预留(空)、TPDO3→2120、TPDO4→2100、TPDO5→2130 | OD.c(1A00–1A04) |
| D3 | 0x2140/0x2150 多余对象 | 删除，语义并入 2130 位图与屏端显示 | OD.h / OD.c / canopen_app.c |
| D4 | TPDO 传输参数未对齐规范 | transmissionType=0xFF + 正确 eventTimer（波形 8ms / 标量状态 1s） | OD.c(0x18xx) |
| D5 | 私有 OD 命名/注释与规范不符（随 D1 一并收敛） | 随 D1 统一为规范命名 | OD.h |
| D6 | 波形采样率被误设为 50Hz | 保持 125Hz（与 UNO 实测采样率一致，eventTimer=8ms） | OD.c(x1800) |
| D7 | 0x2100:01 写的是运行秒(uwTick/1000)，规范要求是 UTC epoch 秒 | 改为软 RTC 日历换算的 UTC epoch 秒 `spo2_rtc_get_epoch()` | app_bloodoxygen.c / canopen_app.c |
| D8 | 标量/波形未用定点单位，规范规定 SpO2/PR=u16/100、NIBP=u16/10、PPG=relative i32/1,000,000 | SpO2/PR×100、NIBP×10、PPG 按 2²² 归一 | canopen_app.c(生产者) |
| D9 | 0x2130:01 位图偏移、未含 PPG 有效位、类型仅为 u8 | 改为 u32 掩码，分区兼容（bit0–3 对齐 MPM + bit31 synthetic + bit4–7 Node-A 扩展），补回 PPG 有效位 | OD.h / canopen_app.c |
| D10 | 标量/状态 TPDO 周期为 100ms，规范建议 1Hz | TPDO3/4/5 eventTimer 改为 1000ms（=1Hz） | OD.c(0x1802/0x1803/0x1804) |
| D11 | NIBP 有效位仅在「曾测量」时置位，无时效语义 | 按真实测量时效维护（有效 + 非测量中 + ≤30min），过期/失败/未测清位，含 uwTick 回绕处理 | app_bp.c / canopen_app.c |

> 验收基线见《NodeA_未完成任务清单》§验收矩阵 NA-11/NA-12：D1–D11 全部完成，仅余 Gateway-C 硬件联调（缺 CAN 收发器/分析仪）。
