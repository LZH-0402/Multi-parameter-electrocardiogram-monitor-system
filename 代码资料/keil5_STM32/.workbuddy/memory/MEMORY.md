# 项目长期约定（keil5_STM32 / 多参数心电监护仪样机）

## 硬件平台
- 主控 MCU：**STM32F407ZGT6**，HSE **8MHz**，主频 **168MHz**（PLL_M=8, PLL_N=336, PLL_P=2）。
- HMI：**大彩（gz-dc）串口屏 DC80480DN050 + VisualTFT 上位机**，接 **USART2 / PA2(TX) PA3(RX)**。
  **波特率 115200**（屏工程 `SPO2_Hospital.tftprj` 的 `DeviceBaudRate="7"`；索引表 3=9600/4=19200/7=115200）。
  MCU 侧唯一开关是 `platform_config.h` 的 `HMI_USART_BAUDRATE`。19200 带宽不够传 PPG 波形。
- 血氧/PPG：**TI AFE4490**（SPI）。
- 下载/调试口：**SWD，PA13=SWDIO、PA14=SWCLK**（芯片硬连线到 Cortex-M4 的 DAP/SW-DP，属 AF0，**代码里无需也无法配置**，全工程未占用这两脚）。ST-Link 20pin 座：1=VTref、7=SWDIO、9=SWCLK，GND 取任一偶数脚。
- 注意：F4 没有 F1 的 `AFIO->MAPR SWJ_CFG`，把 PA13/PA14 配成 GPIO 或其它 AF 就会**直接抢走调试口**导致连不上；进 Stop/Standby 需 `DBGMCU_Config()` 保持调试时钟。
- 用户手头调试器：**ST-Link 与 J-Link 都有**。J-Link 是 **J-OB-V2**（国产 J-Link OB 克隆版，SN 通常为 -1），资料包在 `F:\嵌入式\外设模块资料\Link-OB-V2&J-OB-V2调试器资料20200715\`。工程目录里已有 `JLinkSettings.ini`（Device 为 UNSPECIFIED，用前需在 J-Link Settings 里指定 STM32F407ZG）。
- **J-OB-V2 驱动硬性要求（卖家资料明确写）**：只能用 **6.4 以下**的 J-Link 驱动，**绝不能装 SEGGER 官网最新版**（V6.4+ 会反制克隆固件）。
  - 资料包给的两个补丁：`Segger-5.12e-mdk5.20-J-OB-V2&olink-v1-4P.zip`（优先，文件名直接点名 J-OB-V2）和 `使用文档\Segger-MDK5.15-4.98e-MDK替换文件不提示盗版.zip`（备选，年代更接近 MDK5.14）。两者都含 `Segger/JL2CM3.dll、JLTAgdi.dll、JLink.exe、JLinkARM.dll` + USBDriver，解压后整个 Segger 目录覆盖到 `F:\Program Files\Keil_v514\ARM\Segger\`（先备份）。
  - 完整软件包：`相关软件及驱动\JLink_V5_12.zip`（内含 JLink_V5_12.exe 安装器，提供 USB 驱动 / J-Link Commander / RTT Viewer）。
- J-OB-V2 首次使用：J-Link Commander 里 `usb` 查看 SN；SN=-1 时需 `exec SetSN=<8位>`（**只能改一次**）并逐行执行 `exec addfeature RDI / GDB / JFlash / FlashBP / FlashDL`——**FlashBP 与 FlashDL 不开则 Keil 里 Flash 下载和 Flash 断点用不了**。
- 要用其 VCOM 串口，需先在 5.12+ 驱动下执行 `exec invalidatefw` 强制刷固件（否则个别机器收发少一位）。
- Keil 里 Port 必须强制 **SW**（不要 Auto），SWD 速度先从 1 MHz 起调。
- **本机 Keil_v514 自带的 Segger 驱动是 2015-02 的老版本**（`ARM\Segger\JL2CM3.dll` / `JLinkARM.dll` v4.96），且本机**未安装独立 J-Link 软件包**。用较新的 J-Link（V10/V11）前需先装 SEGGER J-Link Software Pack 并替换 Keil 里的旧 DLL。
- 外设寄存器窗口（System Viewer）由 SVD 驱动：`PACK\Keil\STM32F4xx_DFP\1.0.8\SVD\STM32F40x.svd`，**与用哪个仿真器无关**，ST-Link / J-Link 表现一致。

## 代码约定
- 驱动库：**STM32F4xx 标准外设库 V1.4.0**（StdPeriph，非 HAL），放在 `Libraries\STM32F4xx_StdPeriph_Driver`。
- CMSIS 放在 `Libraries\CMSIS\Core\CM4` 与 `Libraries\CMSIS\Device\STM32F4xx`。
- **板级引脚/串口配置集中在 `STM32\STM32F407ZGT6_Hosptial\inc\platform_config.h`**，换串口换引脚只改这一个文件。
- 外设开关在 `STM32\STM32F407ZGT6_Hosptial\inc\stm32f4xx_conf.h`；加新外设时同步把对应 .c 加进 Keil 工程。
- Keil 工程：**`STM32\STM32F407ZGT6_Hosptial\user\DCDEMO7.uvprojx`**（Target 名 `DCDEMO_F407`，输出 `Obj\DCDEMO7_F407.axf`），宏 `USE_STDPERIPH_DRIVER, STM32F40_41xxx`。
- 时间基准：SysTick = HCLK/8，重装载 `SystemCoreClock/8/100` → **10ms 节拍**（`timer_tick_count`）。

## 应用层结构（2026-09-23 起：血氧监护界面 SPO2_Hospital）
- Keil 工程分组：`User`(main/hw_config/stm32f4xx_it/ulitity) + **`APP`(app_spo2.c / meas.c)** +
  `HMI_DRIVE`(hmi_driver/hmi_user_uart/cmd_queue) + `StdPeriph_Driver` + `CMSIS` + `Startup`。
- **`inc/hmi_ids.h`**：画面/控件 ID 表，唯一权威来源是 VisualTFT 工程的 `.tft` + `output\DefineID.h`，
  改屏后必须重新对照。7 个画面：0 主界面/1 趋势/2 报警记录/3 设置/4 暂停监护/5 报警与探头状态/6 清空报警记录。
- **`app_spo2.c`**：所有界面逻辑与 `NotifyXxx` 回调都在这里；`main.c` 只剩 init + 主循环 + 帧分发。
  下发只调 `hmi_driver.c` 的 `SetXxx()/BatchXxx()`，**不出现协议字节**。
- **`meas.c`**：测量源抽象，`MEAS_USE_AFE4490=0` 走内置演示波形（无硬件即可验证全部画面），
  改 1 则切换真器件（文件底部 3 个 TODO 待补）。
- **`main.c` 不再放界面代码**；屏上传的按键/文本/滑块通知一律在 `app_spo2.c` 处理。
- **源码必须存成 GBK + CRLF**（工程其余文件都是 GBK；屏上中文文本按 GBK 下发，写成 UTF-8 会乱码）。
  新建文件后要显式转码，并抽样验证中文（把 UTF-8 字节按 GBK 解码多数情况下"不报错"，只靠能否解码判断不可靠）。

## 串口屏工程（F:\VisualTFT\SPO2_Hospital）
- `.tft` / `.tftprj` 是 **UTF-8**；`mcu\*` 与 Keil 源码是 **GBK**。
- 按钮交互规则：**凡是要跳画面的按钮一律 `button_type=0` + `switch="中文画面名"`**，
  不依赖 LUA（该屏不支持脚本）。MCU 照样能收到控件通知去拦截决策。
- **自锁按钮（`button_type=1`）上报的是翻转后的新状态，0 也要处理**；瞬变按钮（type=0）只处理 state=1。
- 文字长度必须按控件实际宽度核算：font4=12/6、font5=16/8、font6=24/12、font7=32/16（汉字/ASCII 像素）。
- **图片控件（image）无法从 MCU 切换显示哪张图**——`InstructConfig.xml` 的控件更新子指令只有
  16~20（按钮/文本/进度条/滑块/仪表）。要分级显示只能放多个图片控件用 `SetControlVisiable` 互斥。
- 背光：`SetBackLight(level)` **0 最亮 / 255 最暗**。
- 历史曲线为 **MCU 手动追加**模式（`HistoryGraph_SetValueInt16` 走 `B1 60`），
  MCU 推点周期必须与控件 `sample_cycle` 一致，且不要在追加时重调 `HistoryGraph_SetTimeLength`。

## 命令行编译（改完代码必做）
```
"F:\Program Files\Keil_v514\UV4\UV4.exe" -b <DCDEMO7.uvprojx> -j0 -o <log>   # 增量
"F:\Program Files\Keil_v514\UV4\UV4.exe" -r <DCDEMO7.uvprojx> -j0 -o <log>   # 全量重建
```
- UV4 不往 stdout 输出，必须 `-o` 落盘；日志是 **GBK**。
- 预存告警（可忽略）：hmi_driver.h 的 5 处 `#1295-D` 无参声明 + cmd_queue.h 的 `#1-D` 文件末尾无换行。
  除这两类以外的告警都要当成新增问题处理。

## Keil 调试配置（已定稿：ST-Link 真机）
- **ST-Link**：`UseTarget=1 / UseSimulator=0`，`TargetSelection=11`（uvoptx `nTsel=11`），`<Driver>` 与 `pMon` = `STLink\ST-LINKIII-KEIL_SWO.dll`；Flash 算法 `STM32F4xx_1024.FLM`（1MB）。
- **适配器编号 = `TOOLS.INI` 的 TDRV 序号**（与 uvprojx `TargetSelection` / uvoptx `nTsel` 一一对应，本机 Keil_v514）：TDRV1=UL2CM3(ULINK2/ME Cortex)、**TDRV6=Segger\JL2CM3.dll(J-LINK / J-TRACE Cortex)**、TDRV11=STLink、TDRV12=CMSIS_AGDI。
- **2026-09-23 已改切 J-Link（J-OB-V2）**：uvprojx `TargetSelection=6` + `<TargetDlls/Driver>Segger\JL2CM3.dll`；uvoptx `nTsel=6`、`pMon=Segger\JL2CM3.dll`；`JLinkSettings.ini` `Device="STM32F407ZG"`、`Override=1`（用工程 FLM）。切换只需改这四个值 + 适配器编号（11↔6）。TargetDllName 恒为 SARMCM3.DLL。
- **本机 Keil 自带 Segger 驱动 JLinkARM.dll 4.96d（2015-02）**，未装 SEGGER 官方软件包；4.96<6.4，J-OB-V2 克隆件可直接用。J-Link Commander 在 `Keil_v514\ARM\Segger\JLink.exe`。
- **Keil 开着时改 uvprojx 会被回写覆盖**，必须先关 UV4 再改。
- **F407 不能软件仿真串口**：`DARMSTM.DLL` 只有 F1/F2，不提供 `S2IN/S2OUT`，`ASSIGN` 必报 error 30。串口屏联调只能上真机。
- 若回退仿真：`SimDlgDll=DCM.DLL -pCM4`、`SimDllArguments=-REMAP -MPU`（否则复位向量取不到，报 error 65 @0x0000000C），并用 `COMDEBUG.ini` 里的 MAP 开放外设空间。

## 历史
- 该工程原为 STM32F103 版（大彩 DCDEMO7 例程），2026-09-23 已移植到 F407；F103 原件备份在 `STM32\DCDEMO7_F103_backup\`，移植说明见 `F407移植说明.md`。
