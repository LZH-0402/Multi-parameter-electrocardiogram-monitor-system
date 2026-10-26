# Node-A 未完成任务清单

> **对照基准**
> - 《多参数监护仪_总开发合同与系统方案_v0.7 (1).md》（总方案 / 系统边界 / 验收 §17）
> - 《NodeA项目书.md》（v1.0 实施版，按当前代码写实）
> - 《NodeA项目书_v0.3_规划版.md》（规划版，含 SPI2 直采路线，已被实施版取代）
>
> **当前工程**：`代码资料\STM32F407_HOSPITAL` —— 双屏（血氧屏 + 血压屏）+ 双传感器（AFE4490/UNO + HKB-08B+）已联调，编译 0E/0W，调试字段已清理。
>
> **范围说明**：本文聚焦 **Node-A（成员 A = 你）** 职责范围内"还没做 / 只做了预留 / 已决策替换"的任务；系统级（Node-B、Gateway-C、云端）单独列在 §3 仅作全局视角，不属于本工程交付。

---

## 1. 已完成的任务（基线 / 给信心）

下列均已落地并可对应到项目书的验收矩阵：

| 项 | 内容 | 对应验收 |
|---|---|---|
| 双屏双传感器全链路 | 5 路 UART + 两块大彩屏 + 血氧(UNO中转) + 血压(HKB) 全部打通 | NA-01 / NA-08 / NA-09 |
| 血氧采集与显示 | UNO 17 字节帧解析、SpO2/PR、PPG 波形(自适应归一化+采样环)、趋势、暂停、报警 | NA-02~NA-04 / NA-12~NA-14 |
| 血压全流程 | 唤醒/开始/停止/压力帧/完成帧/错误帧状态机、100 条历史环形缓冲、纯时间闸门气压写屏 | NA-06 / NA-07 |
| 时钟同源 | 两屏均用屏内 RTC 控件 + `EE 81` 每秒校准，软 RTC 基准共用 `g_rtc_epoch_tick` | NA-07 / NA-15 / NA-16 / NA-17 |
| 异常可诊断 | 坏帧/队列溢出/超时均有计数与状态，调试口可查 | NA-10 |
| 信号质量 | 当前由 F407 峰检推导 SQI（**已可用，非单纯 0/100 占位**，详见 §2.2） | NA-03 |
| 干净固件 | `DEBUG_PPG` 调试字段已收口，出干净版本 | — |
| 编译验证 | 全量 armcc 0 error / 0 warning | §9.2 |
| CANopen 上报 Gateway-C | 单实例 CANopenNode v4 栈 + bxCAN 驱动 + 私有 OD 0x2100–0x2130 + 0x2110（PPG 波形在 0x2110 = ir_wave + sample_tick 配对；2140 已在 D3 删除、2150 已在 D2 删除）+ 5 路 TPDO（参考布局，含 PPG 波形 125Hz）+ 适配层 `canopen_app.c`，编译 0E/0W | NA-11 / NA-12 |

> 说明：项目书 v1.0 §11 把 `signal_quality` 记为"二值占位"，那是 10-12 编写时的状态；本次调试已把 UNO 中断占位写移除，改由 F407 `HMI_WaveGetSQI()` 写入峰检质量，**当前已是连续 0~100 值**，故不再视为"占位"。

---

## 2. Node-A 范围内未完成的任务

### 2.1 必须做 / 硬缺口（直接影响总方案验收 §17）

**【已闭环】CANopen 上报 Gateway-C（✅ 已实现，2026-10-15）**

> ✅ **实现说明**：已完整落地，协议细节见 `其他资料/CANopen_协议扩展_NodeA.md`。要点：
> - **CANopenNode v4 单实例栈**（`CO_USE_GLOBALS` 静态对象模式，免 heap，适配裸机），Node-A = Node-ID **1**，**500 kbps**（PA11/RX、PA12/TX，AF9），`CO_NMT_STARTUP_TO_OPERATIONAL` 上电自主进入 OPERATIONAL（无需 NMT 主机）。
> - **bxCAN 驱动** `HARDWARE/CANopen/CO_driver.c` + `CO_driver_target.h`（移植自 CANopenNode，已修正 SPL 风格中断使能宏 / 关闭时钟写法，LSS slave 纳入编译）。
> - **对象字典**：DS-301 标准通信对象（NMT/SDO/EMCY/Heartbeat/TIME/SYNC）+ 私有区 **0x2100–0x2130 + 0x2110**（标量/状态；PPG 波形在 **0x2110**，ir_wave + sample_tick 配对；2150 已在 D2 删除、2140 已在 D3 删除），子对象置 `ODA_TPDO` 可映射。`ODList[]` 严格按索引升序排列（CANopenNode v4 `OD_find()` 为二分查找，乱序会导致对象找不到——D3 顺手修正了 D1 把 2110 追加在 2130 之后造成的乱序）。
> - **5 路 TPDO（D2 重排为参考布局）**：TPDO1 `0x181`→2110 PPG 波形（eventTimer=**8ms = 125 样点/秒**，与 UNO(AFE4490) 实测采样率对齐）；TPDO2 `0x281`→2111 第二波形（**Node-A 保留、不发送**，eventTimer=0 禁用）；TPDO3 `0x381`→2120 标量（SpO2/PR/NIBP_SYS/DIA，eventTimer=100ms）；TPDO4 `0x481`→2100 时钟（epoch/tick，eventTimer=100ms）；TPDO5 `0x681`→2130 有效性+启动代次（2130:01=u32 有效/质量位掩码、bit31=synthetic，2130:02=u32 boot_generation；eventTimer=100ms）。"不可变快照"由生产者每轮先写 OD 再 `CO_process_TPDO` 保证。
> - **波形源**：UNO 每帧 1 个 IR 原始采样；在 USART6 中断里**同时**推入屏显示环与新增 CAN 专用环 `uno_wave_can`（每点配对采样时刻 uwTick），两消费者解耦互不争抢；生产者 `canopen_app_produce()` 取点写 0x2110（ir_wave + 配对 sample_tick）并 `CO_TPDOsendRequest` 请求发送。
> - **适配层** `HARDWARE/CANopen/canopen_app.c/.h`：初始化 / 主循环 `CO_process` / 生产者写 OD（含 `data_valid_flags` bit0–bit6）/ 三路 CAN 中断统一入口 / EMCY 封装；`main.c` 接入 init+process，`stm32f4xx_it.c` 接入 CAN1_RX0/TX/SCE。
> - **编译**：armcc AC5 整工程 **0 Error(s) / 0 Warning(s)**（D10/D11 后 Code=44316 RO=3012 RW=1480 ZI=15552；D9 后 Code=44240；D8 后 Code=44228；D7 后 Code=43896；D4 后 Code=43708 RO=2988；D3 后 Code=43712；D2 后 Code=43732；D1 基线为 Code=43764 RO=3240 RW=1576 ZI=15920）。
> - **⚠️ 唯一未闭环项**：开发环境**无 CAN 收发器/分析仪**，未做总线在线回环 / 接 Gateway-C 联调；协议已冻结（md §6 Gateway 镜像要求：建相同私有 OD + 5 路 RPDO 对应 0x181~0x681，其中 0x281 为 Node-A 保留不发），待硬件到位后按 §6 配 RPDO 实测 100ms 标量/状态帧 + 8ms 波形帧（波形在 0x181）。

> **【D1/D2/D3 对照 v0.1 规范整改】（进行中）**：原实现（6 路 TPDO、波形挂在 TPDO6/0x681、私有 OD 含 0x2150/0x2140）与三份 v0.1 规范（《CANopen_PDO映射_v0.1》《CANopen_对象字典_v0.1》《MPM_统一数据字典_v0.1》）不一致。已确认方向一（改代码对齐规范）：
> - **D1（✅ 已完成）**：新建参考定义的 **2110** 主波形对象，把 PPG 波形从 2160 迁到 2110，并配采样 tick（ir_wave + sample_tick 配对，兼解 D5 缺 tick）；整工程编译 0E/0W 通过。
> - **D2（✅ 已完成）**：删除 **0x2150**（NIBP 脉率/血压状态机，参考规范无此索引；其 NIBP 状态已由 2130:01 的 DV_NIBP_* 位承载，NIBP 脉率与 SpO2 脉率同源，暂丢弃，已在 D3 确认丢弃）并把 TPDO 重排为参考 5 路布局（波形迁到 TPDO1/x1800，0x681 让给 TPDO5/x1804→2130）。`OD_CNT_TPDO` 6→5；生产者 `CO_TPDOsendRequest` 由 `TPDO[5]` 改 `TPDO[0]`。整工程编译 0E/0W 通过（Code=43732）。
> - **D3（✅ 已完成）**：删除 **0x2140**（SQI/probe，参考规范对象字典仅 2100/2110/2111/2120/2130，无 2140）；其中 `probe_state` 已由 2130:01 的 `DV_PROBE_ON` 位承载，连续 SQI 在参考规范无对应字段、不再单独上送（屏端仍用 `g_meas.signal_quality` 显示）。顺带修正 D1 把 2110 追加在 2130 之后造成的 `ODList[]` 乱序（CANopenNode v4 `OD_find()` 二分查找要求升序，乱序会让 2110 找不到），现已严格升序。整工程编译 0E/0W 通过（Code=43712）。
> - **D4（✅ 已完成）**：2130 改 u32+u32 并加 synthetic 位：2130:01 由 uint8 `data_valid_flags` 扩为 u32 质量/有效位掩码（bit31=synthetic，真实 A/B 数据恒 0、仅合成/回放测试数据置 1）；2130:02 由 uint16 `boot_generation` 扩为 u32。同步修正 TPDO5(0x681) 映射位宽（`0x21300108`→`0x21300120`、`0x21300210`→`0x21300220`，即 u8+u16→u32+u32，PDO 帧由 3B→8B），OD.c 记录块 `dataLength`(1→4、2→4)。`canopen_app.c` 的 `flags`/`s_boot_generation` 改 `uint32_t`。整工程编译 0E/0W 通过（Code=43708）。
> - **D5（✅ 已完成，随 D1 一并闭环）**：PPG 波形采样 tick 配对（ir_wave + sample_tick）。在 D1 新建 2110 时即已配对写入 `x2110_ppg_waveform.sample_tick`（与 2100:02 同域、均来自 uwTick），故 D5 无需额外改动；整工程编译 0E/0W 通过。
> - **D6（⏭️ 用户决策跳过）**：波形速率 125Hz→50Hz 的降速。**用户明确保持 125Hz（eventTimer=8ms）**，与 UNO(AFE4490) 实测采样率对齐，不降速到 50Hz（20ms）。该项从待办移除。
> - **D7（✅ 已完成）**：2100:01 由"上电运行秒(uwTick/1000)"改为"UTC epoch 秒"。新增 `spo2_rtc_get_epoch()`（`app_bloodoxygen.c`，把软 RTC 日历 `s_year/s_mon/s_day/s_sec_of_day` 换算成 Unix epoch 秒，含 100/400 闰年修正，不含 `SPO2_RTC_OFFSET_SEC` 屏显补偿）；`canopen_app_produce()` 写 `x2100_epoch_tick.epoch = spo2_rtc_get_epoch()`；2100:02 仍写节点单调毫秒节拍 `uwTick`（与 MPM 统一数据字典 v0.1 2100:02 对齐）。整工程编译 0E/0W 通过（Code=43896 RO=3012 RW=1480 ZI=15544）。
> - **D8（✅ 已完成）**：定点单位对齐 MPM 统一数据字典 v0.1 的"单位 / CAN 固定点类型"。`canopen_app_produce()` 中：SpO2/PR 由裸整数改为 `×100`（u16/100，如 98.50%→9850）；NIBP_SYS/DIA 改为 `×10`（u16/10，如 120.5mmHg→1205）；PPG `ir_wave` 由裸 AFE4490 ADC 计数改为"按 ADC 满量程 2^22 归一的相对值 ×1,000,000"（relative, i32/1,000,000，用户确认用 2^22），用 int64 中间量防溢出。字段位宽（u16/u32）不变，故 `OD.h`/`OD.c` 无需改；CAN 专用环与屏端裸计数/自适应归一化均不受影响。整工程编译 0E/0W 通过（Code=44228 RO=3012 RW=1480 ZI=15544）。
> - **D9（✅ 已完成）**：2130:01 有效位位图规范化（分区兼容方案，用户确认）。将原偏移一位的 SpO2/PR 位重排回 MPM 定义，并**补全原本缺失的 PPG 有效位**：bit0=PPG 有效（MPM A bit0，新增，条件=探头接入）、bit1=SpO2 有效、bit2=PR 有效、bit3=NIBP 有效（MPM A bit3）、bit31=synthetic；bit4-7 作为 Node-A 厂商扩展保留 探头接入/NIBP测量中/NIBP停止中/波形有采样可取（与 MPM 区 0-3、31 不冲突）。仅改 `canopen_app.c` 的 `DV_*` 宏 + 组装逻辑（新增 `DV_PPG_VALID`），`OD.h`/`OD.c` 不动；bit3 的时效语义留 D11。整工程编译 0E/0W 通过（Code=44240 RO=3012 RW=1480 ZI=15544）。
> - **D10（✅ 已完成）**：标量周期对齐 MPM 的 1Hz。TPDO3/4/5（0x381/0x481/0x681，分别映射 2120 标量 / 2100 时钟 / 2130 有效性）的 `eventTimer` 由 100ms(0x0064) 改为 1000ms(0x03E8)；三者 `transmissionType` 均为 0xFF（事件触发 + eventTimer 周期），改后即 1Hz 周期上报。波形 TPDO1 保持 8ms(125Hz，D6 用户决策)；TPDO2(0x281) 仍 eventTimer=0 禁用。仅改 `OD.c` 三处 `eventTimer` 初值 + `canopen_app.c` 头部注释（100ms→1000ms）；`OD.h` 不动。整工程编译 0E/0W 通过。
> - **D11（✅ 已完成）**：NIBP 有效位按真实测量时效维护（MPM：过期/失败/未测量必须清位）。`bp_last_t` 增 `valid_tick`（测量成功完成时的 uwTick，app_bp.c:1274 记录）；`canopen_app_produce()` 中 NIBP 有效位(bit3) = `last.valid` 且 当前不在新一轮测量/停止中（旧结果被新测量取代）且 `nibp_age ≤ NIBP_STALE_MS`；`nibp_age` 含 uwTick 32 位回绕处理。`NIBP_STALE_MS` 默认 30min(1800000)，注释明确为可调阈值（若自动测量间隔更长应调大）。屏端仍按 `s_last.valid` 显示上次结果，CAN 有效位与屏显示解耦。整工程编译 0E/0W 通过（Code=44316 RO=3012 RW=1480 ZI=15552；ZI 较 D9 +8 因 bp_last_t 增 4B 字段）。
> - **✅ D1–D11 全部完成**：私有 OD / 5 路 TPDO / 定点单位 / 有效位位图 / 时钟 epoch / 波形采样 tick / 标量 1Hz / NIBP 时效 均已对齐三份 v0.1 规范，整工程 0E/0W。仅余 **Gateway-C 硬件联调**（缺 CAN 收发器/分析仪）。

- **原要求**（总方案 §8.3、项目书 v0.3 §8、验收 NA-11/NA-12，均已满足）：
  - `CAN1 + TJA1050` 外设初始化（波特率、终端电阻、总线隔离按 §3.6）；✅
  - **单一 Node-A CANopen 实例**（一个 Node-ID、一份 OD、一套 NMT/Heartbeat/EMCY，血氧与血压作为同一节点下的两个业务域，禁止复制两个单模块例程）；✅
  - 对象字典与 PDO 映射（由 P 冻结：`spo2 / spo2_pr_bpm / spo2_quality / probe_state / pressure_mmHg / sys_mmHg / dia_mmHg / nibp_pr_bpm / bp_state / 各类有效位` 等，见 v0.3 §8.2 → 现已扩展为私有 OD 0x2100–0x2130 + 0x2110 等价覆盖（2140/2150 已按 v0.1 规范删除）；✅
  - TPDO 周期发送"不可变快照"、Heartbeat 表在线、EMCY 报关键故障；✅（快照由生产者时序保证；Heartbeat/EMCY 走标准 DS-301）
  - 回环自测 → 接 Gateway-C 联调；🟡 代码与协议已就绪，待硬件联调（见上方 ⚠️）。
- **原建议拆子任务（均已落地）**：① 与 P 冻结协议（采用私有 OD 扩展方案，已写 md）；② `can_port.c` 等价物（`CO_driver.c`）；③ 引入单实例 CANopenNode 栈；④ 适配层映射 `g_meas`+血压状态进 OD/TPDO（`canopen_app.c`）；⑤ 回环 + 接 Gateway-C 验证（待硬件）。

### 2.2 已识别技术债 / 深化项（项目书 v1.0 §11 已知限制 + v0.3 要求）

| 项 | 现状 | 后续动作 |
|---|---|---|
| **气路安全** | 依赖人工泄压；`A1` 只证明模块收到停止命令，不代表气路已泄压（§5.5/§11） | 增加独立压力传感反馈 + 硬线断泵路径（**安全相关，建议优先**） |
| **spo2/hr 时效标注** | 算法算完才更新，其余时间保持旧值（§4.4/§11） | ✅ **已实现**（2026-10-14）：`measurement_t` 增 `result_time_ms`/`age_ms`；`uno.c` 每解帧成功刷新时间戳、`on_uno_frame` 新 PR 亦刷新；`SPO2_STALE_MS=4000` 判定陈旧；陈旧后主界面/趋势"最近值"转 `--`、状态显"数据陈旧"、报警释放、趋势停记。AC5 全量编译 0E/0W 通过 |
| **看门狗 IWDG** | v0.3 §4.2 要求监督服务统一喂狗；原应用层未实现 IWDG | ✅ **已实现**（2026-10-14）：新增 `HARDWARE/SYS/sys_wdt.c/.h`；`WDT_Init()` 启动前读 `RCC_FLAG_IWDGRST` 记录 `g_reset_reason`（供统一诊断快照复用）并清标志；IWDG prescaler=128/reload=625（LSI 32kHz 典型超时 10.0s，范围 6.82~18.9s），规避 9600bps 血压屏阻塞发送尖峰导致的误复位；`WDT_Feed()` 仅在主循环 `while(1)` 顶部调用，外设离线不停喂、不误复位；不在中断喂狗。AC5 全量编译 0E/0W 通过 |
| **统一诊断快照 `node_status_t`** | 当前 `g_meas` + 血压侧结构，缺 `boot_id/uptime/各类在线标志/队列高峰水位/reset_reason`（v0.3 §4.4） | 落地 v0.3 §4.4 快照结构，供屏/日志/CANopen 共用同一份 |
| **屏在线探测与重握手** | v0.3 §7.4 要求 `INIT/ONLINE/OFFLINE/RESYNC` + 每 1s 查询、连续 3 次无回复判失联；原未实现自动探测/重握手 | ✅ **已实现**（2026-10-14）：新增 `HARDWARE/SYS/screen_watch.c/.h` 通用看屏模块，每屏独立状态机 `INIT/ONLINE/OFFLINE/RESYNC`。参数由 9600bps 推导：**探测周期 1000ms**（单轮查询 9B 下行+11B 上行≈21ms 线路）、**响应超时 150ms**（约 7×线路时间）、**失联阈值 3（可调，`screen_watch_set_miss_thresh`）**、离线**蜂鸣报警** 1000ms 且每 3s 重复。`main.c` 注册两屏回调：`hmi_get_screen`/`hmi_beep`/`bp_resync`（血压屏）、`HMI_GetScreen`/`HMI_Beep`/`spo2_resync`（血氧屏）；`screen_watch_tick()` 每主循环调用。探活以各屏对 `EE B1 01` 查询的回复为准（BP：`hmi_rx_frame` 的 `B1 01` 分支 `screen_watch_touch`；SPO2：`NotifyScreen` 覆盖 dc_hmi 弱实现 `screen_watch_touch`）。**重握手**：① 血压屏 `bp_resync()` 只恢复显示（时钟+状态栏+强制回原页+依页补刷+历史页 `hist_show_page` 重发全部历史记录），**绝不重发 `0xA0` 启动测量**；② 血氧屏 `spo2_resync()` 回放 MCU 侧保存的趋势历史副本 `s_hist_spo2/s_hist_pr`（环形 900 点=30 分钟@2s）到 `TRD_GRAPH_SPO2/PR`，分片（每 50ms≤6 点）避免阻塞，实现"重发全部历史"。在线标志 `screen_watch_online()` 已暴露，供 §2.1 `node_status_t`(CANopen) 复用。AC5 全量编译 0E/0W 通过 |
| **信号质量 SQI 深化** | 已实现为 F407 峰检 SQI（可用），但非"真实灌注指数 PI" | 可选：UNO 侧输出 PI，或 F407 基于 AC/DC 估算更佳质量 |
| **血氧算法时基** | 实测 UNO loop ≈77~121Hz（delay(8)+SPI 采样+17B@57600 发送）。①**显示侧已对齐**：F407 `dc_hmi.c` 的 PR/波形流水线已按实测速率重定标（`PPG_FS_HZ=121`、滤波/最小峰距/PR 超时/包络全按 121Hz），PR 与波形显示与时基一致——此前"25Hz 假设不符 → PR 错"在显示侧已消除。②**UNO 端 SpO2 库残余**：`S_afe44xx.cpp` 仍以 `dec==20` 降采样填 SpO2 缓冲（`SF_spo2=25`、`BUFFER_SIZE=100` 未动），有效 SpO2 缓冲速率仅 4~6Hz，血氧值约每 16~26s 刷新一次 → 数值稳定正确但收敛/刷新偏慢。 | 降级为**已知限制**（血氧值正确、仅刷新慢）；如需加快可改 `dec` 系数或重定标 `SF_spo2`（属刷新优化非错误，可暂缓）。与 §2.1 CANopen 前的 24h 实测一并验证 |
| **血氧刷新加速（UNO 端方案A 已实施）** | SpO2 刷新已由 ~16.5s 提到 **~2s**（方案A，2026-10-14，只改 UNO 库、F407 零改动）：`S_afe44xx.cpp` 的 `dec 20→5`（输入率回 25Hz 设计值）+ 窗口 `100→50`；`S_spo2_algorithm.h` 的 `BUFFER_SIZE` 由 `SF_spo2*4` 改为 `*2`（=50，必须随窗口同步改，因算法内部用 BUFFER_SIZE 做平滑/阈值/峰值检测）。UNO(ATmega328P) SRAM≈2KB，高输入率+长窗口才会爆内存。 | **已实施**，详见 2.2.1（含后果与验证）。激进方案B（~1s，窗口25/`BUFFER_SIZE`=25）仍可选，与 §2.1 CANopen 前 24h 实测一并验证 SpO2 跳动/无效值是否可接受。 |
| **DMA 优化（可选）** | v0.3 §4.2 建议 SPI2 双向 DMA + UART RX DMA；当前 `RX_USE_DMA_IDLE=0` 全 RXNE（§7.3 判定"够用"） | 仅当带宽吃紧时再做；属性能优化非功能缺口 |

#### 2.2.1 血氧刷新加速（UNO 端最小改动，方案A 已实施 / 方案B 可选）

- **现状与根因**：SpO2 约 **16.5s** 才刷新一次（= `BUFFER_SIZE=100` 点 ÷ 有效输入率 ~6Hz）。
  有效输入率仅 ~6Hz 是因为 `S_afe44xx.cpp` 里 `if (dec==20)`（每 20 次采样才填 1 点），而库设计的 `SF_spo2=25Hz`，二者已不匹配（当前反而**欠采样**）。
  每次 `n_buffer_count>99` 才调 `estimate_spo2(...)`，即 4s@25Hz 窗口。
- **两处可调杠杆（都只在 UNO 库，F407 一行不动）**：
  1. `S_afe44xx.cpp` 的 `dec` 阈值（降采样比） → 改输入率；
  2. `S_spo2_algorithm.h` 的 `SF_spo2`/`BUFFER_SIZE`，以及 `S_afe44xx.cpp` 里的 `n_buffer_count>99` 门槛与 `estimate_spo2(...,100,...)` 的长度参数 → 改窗口。
- **RAM 约束**：UNO ≈2KB SRAM。若把输入率拉到 121Hz（`dec=1`）且保留 4s 窗口 = 484 点/通道 ×2 通道 ×2B ≈ **1.9KB**，再加 `hral` 心率对象/其它全局/栈就溢出——所以**不能靠"高输入率+长窗口"加速**，只能是"中高输入率 + 短窗口"。
- **✅ 方案 A 已实施（2026-10-14）**：只改 UNO 库、F407 零改动。四处联动：`S_afe44xx.cpp` ① `dec 20→5`（输入率 6→~25Hz，回到库设计值、反而更准）② `n_buffer_count >99→>49` ③ `estimate_spo2` 长度 `100→50`；`S_spo2_algorithm.h` ④ `BUFFER_SIZE` 由 `SF_spo2*4` 改 `*2`（=50）。刷新 16.5s→**~2s**；（`aun_ir_buffer` 等硬件数组仍为 `[100]`，≥50 安全）。
- **激进方案 B（刷新 ~1s，更抖，尚未实施）**：在 A 的 `dec=5` 基础上把窗口进一步 `>49→>24`、`estimate_spo2` 长度 `50→25`、`BUFFER_SIZE` `*2→*1`（=25）。刷新→**~1s**，但窗口仅 ~1~2 个心动周期，SpO2 噪声/跳动明显增大、更易出无效值。
- **实施必改一致项（关键）**：`n_buffer_count` 门槛、`estimate_spo2` 的**长度**、以及 `S_spo2_algorithm.h` 的 **`BUFFER_SIZE` 三者必须同时 = 窗口大小**（A=50 / B=25）。原因：`spo2_algorithm::estimate_spo2` 内部对 `an_x` 做平滑/算阈值/找峰值时使用的是全局 `BUFFER_SIZE` 而非传入的 `n_ir_buffer_length`（见 `S_spo2_algorithm.cpp` 第 53/61/84 行）——若只把传入长度改小而 `BUFFER_SIZE` 仍是 100，后半段 `an_x[50..99]` 会残留上一轮的陈旧数据，导致阈值与峰值检测被污染、SpO2 算错。**只改其一会数组越界、少算或算错**，是主要实现坑。
- **后果清单**：
  ① 刷新 16.5s → 2s（或 1s）；
  ② **PR/HR 不受影响**——`hral.statHRMAlgo` 每个原始采样都跑（与 `dec`/窗口无关），且 F407 的 PR 是**自己峰检**；
  ③ 窗口越短平均越少，SpO2 噪声/跳动越大，**建议不低于 2s 窗口**；
  ④ F407 已有时效/陈旧保护（`SPO2_STALE_MS=4000`、无效值保持上次读数），2s/1s 刷新均不会误触发"数据陈旧"；
  ⑤ `MA4_SIZE=4` 是算法内部平滑长度，**勿动**；
  ⑥ 仅改 UNO 参数，F407 侧 `dc_hmi.c`/`app_bloodoxygen.c` 零改动，F407 只是更频繁读到新的 SpO2 字段。
- **验证**：改完用 24h 实测对照（与 §2.1 CANopen 前一同验证），重点看 SpO2 跳动幅度与偶发无效值是否可接受。

### 2.3 一期不强制 / 预留（总方案 §12.1：保留接口，不作为一期强制完成项）

- **ESP8266-A 冷备 MQTT/TLS 直连**：仅 UART4 接口在 v0.3 §3.2 标注预留，**无驱动、无 TLS、无自动切换**。正常公网出口是 Gateway-C + ESP8266-C（P 负责）。本项一期可不做，保留软件扩展位置即可。
- **最终单板样机集成**（总方案 §17 评审项 A）：待测试后由评审决定，当前不承诺。

### 2.4 路线变更说明（**不是"未完成"，而是已决策替换**）

- v0.3 规划的 **"AFE4490 经 SPI2 直连 F407 + F407 本地运行血氧/脉率算法"** 路线，在 `STM32F407_HOSPITAL` 中**未采用**；实施版 §1.3 已明确改为 **"AFE4490 → Arduino UNO 中转 → USART6 → F407"**。
- 因此"SPI2 直采未做"是**有意的路线选择**，不是遗漏。若未来要去掉 UNO，按 v0.3 §3.3/§5.1 的门槛清单推进，且 `measurement_t` 接口保持不变、业务层代码基本不动（实施版 §4.1 刻意保留的兼容性）。
- 验收矩阵里 `NA-02 / NA-18 / NA-19`（针对 SPI2 直采 + DRDY + 寄存器回读）在 UNO 路线下**不适用**，应替换为对 UNO 帧链路（17 字节帧、诊断环、解帧状态机）的验收。

---

## 3. 系统级未完成任务（非 Node-A 职责，仅作全局视角）

下列是整个项目的其余大块，由 **成员 B / 负责人 P** 负责，不在本工程内；但 §2.1 的 CANopen 上报是打通全链路的前置：

| 块 | 负责 | 内容 | 与本工程关系 |
|---|---|---|---|
| Node-B | 成员 B | 体温 TEMP（GY-641V3 I2C）、ECG/HR（ADS1292R SPI+DRDY）、RESP_RAW 采集、两屏 | 平行节点，通过 CANopen 汇聚 |
| RR 算法 | 负责人 P | RESP_RAW → RR，PC/Python 参考 + STM32 版，集成到 Node-B | 不归你 |
| Gateway-C | 负责人 P | CANopen 汇聚、MQTT Client、ESP8266-C、离线缓存、断网补传 | **你的 CANopen 上报必须和它对接** |
| 云端 + Web | 负责人 P | MQTT Broker、Backend、Nginx、Web 大屏、WebSocket 实时波形 | 不直接归你 |
| 全链路 24h 稳定性 | 全员 | 总方案 §14.6 / §17 | 你的 24h 实测是其中一部分 |

---

## 4. 建议的下一步优先级

1. ~~**【关键路径】CANopen 上报（§2.1）**~~ **✅ CANopen 上报（§2.1）已实现（2026-10-15）**：协议栈 / 私有 OD / 6 路 TPDO（含 PPG 波形 125Hz）/ 适配层 / 编译均已完成，仅余 **Gateway-C 硬件联调**（待 CAN 收发器/分析仪到位，见 §2.1 ⚠️）。
2. **技术债按风险排序（剔除已完成的时效标注 / 看门狗 / 屏探测）**：气路安全（安全，优先）> 统一诊断快照 `node_status_t`（§2.2，可复用 `screen_watch_online()` 等现有在线标志）> SQI/算法深化 > DMA 优化。
3. **冷备 ESP-A（§2.3）**：一期不强制，CAN 联调通过后再作为加分项。
4. **测试动作闭环**：① 跑一次 24h 综合稳定性（NA-11/NA-16/NA-17 的"实测"部分目前是"功能已实现、尚未跑测"）；② 补 CANopen 与 Gateway-C 的**硬件联调**（§2.1 ⚠️ 项）—— 在 Gateway-C 侧按 md §6 配 5 路 RPDO（0x181/0x281/0x381/0x481/0x681；其中 0x281 为 Node-A 保留不发）后实测 100ms 标量/状态帧 + 8ms 波形帧（波形在 0x181）。

---

## 5. 验收矩阵缺口速查

| 验收项 | 状态 | 备注 |
|---|---|---|
| NA-01~NA-10 | ✅ 功能已实现 | 双屏/双传感器/历史/异常诊断 |
| NA-12~NA-15 | ✅ 功能已实现 | PPG 连续性、时钟同源、节流等价 |
| NA-16 / NA-17 | 🟡 功能具备，待实测 | 需"逐秒对照三处时间 + 连续 5 分钟观察"的测试动作 |
| NA-11（24h） | 🔴 未跑 | 需实跑一次 24h 稳定性并记录 |
| NA-02 / NA-18 / NA-19（SPI2 直采） | ⚪ 不适用 | UNO 路线下替换为对 UNO 帧链路的验收 |
| **NA-11 / NA-12（CANopen）** | 🟡 功能已实现，待 Gateway-C 硬件联调 | §2.1 已闭环（**D1–D11 全部完成**，对齐三份 v0.1 规范）：5 路 TPDO（波形 125Hz，保持不降速；标量/时钟/有效性 1Hz）、私有 OD 仅 2100/2110/2111/2120/2130（2140/2150 已删）、2130 u32+u32 含 synthetic 位、2100:01 为 UTC epoch 秒、2110 波形配 sample_tick、2120 标量按 MPM 定点单位（SpO2/PR u16/100、NIBP u16/10）、2110 PPG 按 ADC 满量程 2^22 归一 relative i32/1,000,000、2130:01 有效位位图分区对齐 MPM（bit0-3=PPG/SpO2/PR/NIBP 有效 + bit31 synthetic，bit4-7 为 Node-A 扩展）、NIBP 有效位按时效维护（过期/取代/超时清位）；协议见 `其他资料/CANopen_协议扩展_NodeA.md` §6（Gateway 侧 5 路 RPDO 0x181~0x681，0x281 为 Node-A 保留不发）；⚠️ 缺 CAN 收发器/分析仪未跑总线实测 |
| 系统级 NA-11/12/13/17（三板在线） | 🔴 依赖 B/P | Node-B / Gateway / 云未交付 |

图例：✅ 已完成 · 🟡 功能在、待实测/联调 · 🔴 未做/未跑/依赖他人 · ⚪ 不适用
