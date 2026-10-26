# ============================================================================
#  SUPERSEDED / 退役脚本（仅供历史参考）
#  当前 OD.h / OD.c 已改为手工维护。D1（2110 波形对象）、D2（删 2150、TPDO 重排为
#  参考 5 路布局）等均直接改 C 文件完成。重跑本脚本会回退这些改动，请勿执行。
#  本脚本仅保留为“最初对象字典是怎么拼出来的”的过程记录。以下 TPDO_COB / TPDO_MAP
#  已按 D2 后的实际布局修正（x1804 COB=0x681；1A04 映射 2130；2150 已在 D2 删除）。
# ============================================================================
import re
BASE = r"F:/多参数心电监护仪系统-样机开发/代码资料/STM32F407_HOSPITAL/HARDWARE/CANopen"
odc = open(BASE + "/OD.c.bak", "r", encoding="utf-8").read()

NODE_ID = 1
TPDO_COB = {0x1800: 0x180 + NODE_ID, 0x1801: 0x280 + NODE_ID, 0x1802: 0x380 + NODE_ID,
            0x1803: 0x480 + NODE_ID, 0x1804: 0x680 + NODE_ID}
# 修正为 D2 后的实际 5 路 TPDO 映射布局：
#   TPDO1(x1800/1A00) -> 2110 波形；TPDO2(x1801/1A01) 保留不发(2111 不在 Node-A)；
#   TPDO3(x1802/1A02) -> 2120 标量；TPDO4(x1803/1A03) -> 2100 时钟；
#   TPDO5(x1804/1A04) -> 2130 有效性。2150 已在 D2 删除。
TPDO_MAP = {
    0x1A00: (0x2110, [(1, 0x21100120), (2, 0x21100220)]),
    0x1A01: (0x0000, []),
    0x1A02: (0x2120, [(1, 0x21200110), (2, 0x21200210), (3, 0x21200310), (4, 0x21200410)]),
    0x1A03: (0x2100, [(1, 0x21000120), (2, 0x21000220)]),
    0x1A04: (0x2130, [(1, 0x21300108), (2, 0x21300210)]),
}
def comm_init(idx):
    cob = TPDO_COB[idx]
    return ("    .x%X_TPDOCommunicationParameter = {\n"
            "        .highestSub_indexSupported = 0x06,\n"
            "        .COB_IDUsedByTPDO = 0x%08X,\n"
            "        .transmissionType = 0xFF,\n"
            "        .inhibitTime = 0x0000,\n"
            "        .eventTimer = 0x0064,\n"
            "        .SYNCStartValue = 0x00\n"
            "    }," % (idx, cob))
def map_init(idx):
    obj, entries = TPDO_MAP[idx]
    lines = ["    .x%X_TPDOMappingParameter = {" % idx,
             "        .numberOfMappedApplicationObjectsInPDO = 0x%02X," % len(entries)]
    for i in range(1, 9):
        val = 0x00000000
        for (s, v) in entries:
            if s == i:
                val = v
        lines.append("        .applicationObject%d = 0x%08X," % (i, val))
    lines.append("    },")
    return "\n".join(lines)
def block_pat(header):
    return re.escape(header) + r"[^\n]*\n(?:[^\n]*\n)*?    },?"
def block_pat_struct_end(header):
    return re.escape(header) + r"[^\n]*\n(?:[^\n]*\n)*?    }\n\};"

new_pc_init = comm_init(0x1804) + "\n" + map_init(0x1A04)
# step4
for idx in (0x1800, 0x1801, 0x1802, 0x1803):
    pat = block_pat(".x%X_TPDOCommunicationParameter = {" % idx)
    odc, n = re.subn(pat, comm_init(idx), odc, count=1, flags=re.S)
    assert n == 1, "comm %X n=%d" % (idx, n)
for idx in (0x1A00, 0x1A01, 0x1A02, 0x1A03):
    pat = block_pat(".x%X_TPDOMappingParameter = {" % idx)
    odc, n = re.subn(pat, map_init(idx), odc, count=1, flags=re.S)
    assert n == 1, "map %X n=%d" % (idx, n)
print("after step4 x1200:", odc.find(".x1200_SDOServerParameter = {"))
p = odc.find(".x1A03_TPDOMappingParameter = {")
print("==== 1A03 FULL block after step4 ====")
print(odc[p:p+450])
# step5
pat = block_pat(".x1A03_TPDOMappingParameter = {")
m = re.search(pat, odc, re.S)
print("step5 match len:", (m.end()-m.start()) if m else 0)
odc, n = re.subn(pat, map_init(0x1A03) + "\n" + new_pc_init, odc, count=1, flags=re.S)
assert n == 1, "step5 n=%d" % n
print("after step5 x1200:", odc.find(".x1200_SDOServerParameter = {"))
# step6
pat6 = block_pat_struct_end(".x1200_SDOServerParameter = {")
m6 = re.search(pat6, odc, re.S)
print("step6 match found:", m6 is not None, "len:", (m6.end()-m6.start()) if m6 else 0)
