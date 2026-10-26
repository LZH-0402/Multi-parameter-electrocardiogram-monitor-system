# A/B CANopen 联调接口 v0.1

这是 P 阶段二交付的接口基线，不修改 A/B 工作授权。真正系统仍为三个独立节点；单板三个测试实例仅为本阶段验证手段。

| 负责人 | 接口任务 | 本阶段边界 |
|---|---|---|
| A | Node-ID1；PPG/SpO2/PR/NIBP；HB/TPDO/EMCY/SDO server | 按字典缩放与有效性提供，不改云端 |
| B | Node-ID2；ECG/HR/RESP_RAW/TEMP；预留RR；HB/TPDO/EMCY/SDO server | RR无真实算法输出时清有效位 |
| P | Node-ID3；NMT manager、消费者、时钟/模型/云端/缓存；RR算法后续 | 不代替A/B证明传感器驱动完成 |

引用：[统一数据字典](../设计/MPM_统一数据字典_v0.1.md)、[对象字典](../设计/CANopen_对象字典_v0.1.md)、[PDO映射](../设计/CANopen_PDO映射_v0.1.md)。优先复用固定版本 CANopenNode 与本仓库 `mp_od_init` 的生产者索引；A/B 替换采样驱动，不重写CiA301。

接入前由A/B提供：板卡/收发器引脚与供电、CANH/CANL/地/终端电阻连接、编译版本、各传感器真实采样与滤波周期、单位与标定、有效/无效和故障样例、节点可信时间的获取方式、boot_generation更新方式。未确认硬件时不直接烧录当前loopback测试固件作为正常总线程序。

联调顺序：

1. 每个节点单独PRE-OP启动，500ms心跳，SDO读1018/1017/应用对象；身份/单位核对。
2. Gateway发NMT START；检查TPDO各8字节，ECG250Hz、PPG/RESP50Hz，质量和时钟每秒及变化时更新。
3. 单独断A、单独断B；1500ms超时仅影响该节点，另一节点继续上云。
4. 节点重启更新generation；Gateway丢弃旧半批波形，继续新采样，不拼接、不把旧数据续活。
5. EMCY错误/清除抵达云端；测量无效清位，不能用测试常量代替真实值。
6. CAN断开时MQTT维持Gateway状态；恢复后重新接受PDO。网络断开/恢复验证实时优先和REPLAY隔离。
7. 全程确认真实传感器数据的synthetic位为0；合成测试数据始终为1，任何演示都不混淆来源。

RR：等待B提供带采样率、时间戳、单位、工况标注的真实RESP_RAW后，再做Python reference、算法验证、C移植与Node-B部署。本次只冻结对象与INVALID呈现，不宣称算法完成。
