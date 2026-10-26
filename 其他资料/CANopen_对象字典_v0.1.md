# CANopen 对象字典 v0.1

采用 CANopenNode，固定提交 `9b8beed8367241e96ac03f916cd5a500bcb2cf23`，Apache-2.0。上游源码位于 gateway/third_party/CANopenNode；逐文件 SHA256 在 upstream.json。核心 NMT/HB/PDO/SDO/EMCY 没有自研替换。

| 标准对象 | 说明 | 本版值 |
|---|---|---|
| 1000:00 | Device Type | 0，未声称符合专用医疗 CANopen 设备 Profile |
| 1001:00 | Error Register | CANopenNode 维护 |
| 1014:00 | EMCY COB-ID | 0x80 + Node-ID |
| 1015:00 | EMCY inhibit | 100 × 100µs = 10ms |
| 1016:00..02 | Gateway HB consumer | 2；Node1、Node2，各1500ms |
| 1017:00 | Producer HB | 500ms |
| 1018:00..04 | Identity | 测试占位 vendor=0/product=1/revision=1/serial=Node-ID；不是注册厂商标识 |
| 1200:00..02 | SDO Server | 0x600+ID 请求，0x580+ID 应答 |
| 1280:00..03 | Gateway SDO Client | 默认Node1，可 setup 切换；超时1000ms |
| 1400..1409 / 1600..1609 | Gateway RPDO 参数 / 映射 | A五路+B五路 |
| 1800..1804 / 1A00..1A04 | 生产者 TPDO 参数 / 映射 | 五路，异步 type255 |

私有应用对象供 SDO 读取；采集任务直接更新原始 OD，PDO 核心按映射编码。Gateway 只从 RPDO 映射区读取，不能直接访问测试生产者 OD 来绕过总线。

| 生产者记录 | 子项 | 类型 | Gateway A/B 镜像 |
|---|---|---|---|
| 2100 | 01 epoch秒；02整秒边界tick | u32、u32 | 3100 / 3200 |
| 2110 | 01主波形；02采样tick | i32、u32 | 3110 / 3210 |
| 2111 | 01第二波形；02采样tick | i32、u32 | 3111 / 3211 |
| 2120 | 01..04四个标量槽 | 16bit×4，TEMP有符号 | 3120 / 3220 |
| 2130 | 01有效性/synthetic；02boot_generation | u32、u32 | 3130 / 3230 |

所有应用记录 :00 是最高子索引，时钟/波形/质量为2，标量为4。私有读数不接受 SDO 写入。PDO 映射按经典 CAN 最多8字节；修改映射应在 PRE-OP、先 disable COB-ID、清映射数量、写映射、恢复数量和 COB-ID，交由上游栈校验。

Gateway 提供 NMT manager、HB/EMCY consumer、RPDO consumer 和必要的 SDO client；A/B 负责 producer/SDO server。标准对象维护行为以固定版本核心栈为准，不额外实现第二套状态机。测试验证包括读取 Node-A 1017 得到500ms。

实现入口：gateway/canopen/src/mp_od.c、mp_stack.c、mp_can_driver.c。驱动仅做有界 CAN 帧传输，当前应用单线程运行；所有协议处理、OD 写入和网络等待期间的协作调度在同一主线程。UART3 ISR 只入队，不在中断中调用协议或 JSON。
