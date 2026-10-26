#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 Node-A 的对象字典扩展：
  - 在现有 OD.h / OD.c（由 CANopenEditor 生成、已知可用）基础上做“文本拼接”，
    不手抄 33 个标准对象，仅：
    1) OD_CNT_TPDO 4 -> 5
    2) 向 OD_PERSIST_COMM_t / OD_RAM_t 追加新对象的结构体成员
    3) 重写两段 OD_ENTRY_H 宏（按排序后的索引列表重新生成）
    4) 重写 ODList[]（按排序后的索引列表重新生成）
    5) 修改 8 个既有 TPDO 通信参数(1800-1803)/映射参数(1A00-1A03) 为有效值
    6) 追加 7 个新对象的 PERSIST_COMM/RAM 初始化、ODObjs_t 成员、ODObjs 初始化
新对象：1804/1A04（第5路 TPDO）、2100(epoch/tick)、2120(4标量)、
        2130(有效性+generation)、2140(SQI/probe_state)、2150(NIBP HR/bp_state)

============================================================================
  SUPERSEDED / 退役脚本（请勿再执行）
  当前 OD.h / OD.c 已改为手工维护。后续修订（D1 新建 2110 波形对象并把波形从
  第6路 TPDO 迁到 TPDO1；D2 删除 2150、把 TPDO 重排为参考 5 路布局：
  x1800->2110 / x1801 保留不发 / x1802->2120 / x1803->2100 / x1804->2130）
  均直接在 C 文件上完成。重跑本脚本会回退这些改动（且本脚本从未建模 2110 波形
  对象，无法复现当前 OD），请勿执行。以下 TPDO_COB / TPDO_MAP 已按 D2 后实际
  布局修正，仅作参考：x1804 COB=0x681；1A04 映射 2130；2150 已在 D2 删除。
============================================================================
"""
import re

BASE = r"F:/多参数心电监护仪系统-样机开发/代码资料/STM32F407_HOSPITAL/HARDWARE/CANopen"
ODH = BASE + "/OD.h"
ODC = BASE + "/OD.c"

with open(ODH, "r", encoding="utf-8") as f:
    odh = f.read()
with open(ODC, "r", encoding="utf-8") as f:
    odc = f.read()

NODE_ID = 1

# 每个 TPDO 通信参数对应的 COB-ID（含 NODE_ID），transmissionType=0xFF，eventTimer=100ms
TPDO_COB = {0x1800: 0x180 + NODE_ID, 0x1801: 0x280 + NODE_ID, 0x1802: 0x380 + NODE_ID,
            0x1803: 0x480 + NODE_ID, 0x1804: 0x680 + NODE_ID}
# 每个 TPDO 映射参数对应的被映射对象（index, [ (sub, idx<<16|sub<<8|bits), ... ])
# D2 后实际布局：1A00->2110 波形；1A01->保留不发(2111 不在 Node-A)；1A02->2120 标量；
# 1A03->2100 时钟；1A04->2130 有效性。2150 已在 D2 删除。
TPDO_MAP = {
    0x1A00: (0x2110, [(1, 0x21100120), (2, 0x21100220)]),
    0x1A01: (0x0000, []),
    0x1A02: (0x2120, [(1, 0x21200110), (2, 0x21200210), (3, 0x21200310), (4, 0x21200410)]),
    0x1A03: (0x2100, [(1, 0x21000120), (2, 0x21000220)]),
    0x1A04: (0x2130, [(1, 0x21300108), (2, 0x21300210)]),
}

# ---------- 新对象：OD_PERSIST_COMM_t 结构体成员 ----------
new_pc_struct = """    struct {
        uint8_t highestSub_indexSupported;
        uint32_t COB_IDUsedByTPDO;
        uint8_t transmissionType;
        uint16_t inhibitTime;
        uint16_t eventTimer;
        uint8_t SYNCStartValue;
    } x1804_TPDOCommunicationParameter;
    struct {
        uint8_t numberOfMappedApplicationObjectsInPDO;
        uint32_t applicationObject1;
        uint32_t applicationObject2;
        uint32_t applicationObject3;
        uint32_t applicationObject4;
        uint32_t applicationObject5;
        uint32_t applicationObject6;
        uint32_t applicationObject7;
        uint32_t applicationObject8;
    } x1A04_TPDOMappingParameter;"""

# ---------- 新对象：OD_RAM_t 结构体成员 ----------
new_ram_struct = """    struct {
        uint8_t highestSub_indexSupported;
        uint32_t epoch;
        uint32_t tick;
    } x2100_epoch_tick;
    struct {
        uint8_t highestSub_indexSupported;
        uint16_t SpO2;
        uint16_t PR;
        uint16_t NIBP_SYS;
        uint16_t NIBP_DIA;
    } x2120_vitals;
    struct {
        uint8_t highestSub_indexSupported;
        uint8_t data_valid_flags;
        uint16_t boot_generation;
    } x2130_validity_generation;
    struct {
        uint8_t highestSub_indexSupported;
        uint8_t signal_quality;
        uint8_t probe_state;
    } x2140_sqi_probe;
    struct {
        uint8_t highestSub_indexSupported;
        uint8_t nibp_heart_rate;
        uint8_t bp_state;
    } x2150_nibp_hr_bpstate;"""

# ---------- 新对象：OD_PERSIST_COMM 初始化（1804 通信 + 1A04 映射） ----------
def comm_init(idx):
    cob = TPDO_COB[idx]
    return (
        "    .x%X_TPDOCommunicationParameter = {\n"
        "        .highestSub_indexSupported = 0x06,\n"
        "        .COB_IDUsedByTPDO = 0x%08X,\n"
        "        .transmissionType = 0xFF,\n"
        "        .inhibitTime = 0x0000,\n"
        "        .eventTimer = 0x0064,\n"
        "        .SYNCStartValue = 0x00\n"
        "    }," % (idx, cob)
    )

def map_init(idx):
    obj, entries = TPDO_MAP[idx]
    lines = ["    .x%X_TPDOMappingParameter = {" % idx,
             "        .numberOfMappedApplicationObjectsInPDO = 0x%02X," % len(entries)]
    for i in range(1, 9):
        # 找到该 subindex 的映射值
        val = 0x00000000
        for (s, v) in entries:
            if s == i:
                val = v
        lines.append("        .applicationObject%d = 0x%08X," % (i, val))
    lines.append("    },")
    return "\n".join(lines)

new_pc_init = comm_init(0x1804) + "\n" + map_init(0x1A04)

# 稳健块匹配：从 header 起按整行吞咽，直到第一个“    },?\n”（块自身闭合）。
# 成员行均以 8 空格开头（.xxx = ...），不会以“    }”开头，故必停在本块闭合，不会越界。
def block_pat(header):
    # 匹配到“    },?”（块自身闭合）为止，但不吞掉其后的换行/分号，
    # 以免末成员“    }\n};”被合并成“    },};”导致后续步骤找不到闭合。
    return re.escape(header) + r"[^\n]*\n(?:[^\n]*\n)*?    },?"

# 末成员块：闭合为“    }\n};”（无逗号，整个结构体结束）
def block_pat_struct_end(header):
    return re.escape(header) + r"[^\n]*\n(?:[^\n]*\n)*?    }\n\};"

# ---------- 新对象：OD_RAM 初始化 ----------
new_ram_init = """    .x2100_epoch_tick = {
        .highestSub_indexSupported = 0x02,
        .epoch = 0x00000000,
        .tick = 0x00000000
    },
    .x2120_vitals = {
        .highestSub_indexSupported = 0x04,
        .SpO2 = 0x0000,
        .PR = 0x0000,
        .NIBP_SYS = 0x0000,
        .NIBP_DIA = 0x0000
    },
    .x2130_validity_generation = {
        .highestSub_indexSupported = 0x02,
        .data_valid_flags = 0x00,
        .boot_generation = 0x0000
    },
    .x2140_sqi_probe = {
        .highestSub_indexSupported = 0x02,
        .signal_quality = 0x00,
        .probe_state = 0x00
    },
    .x2150_nibp_hr_bpstate = {
        .highestSub_indexSupported = 0x02,
        .nibp_heart_rate = 0x00,
        .bp_state = 0x00
    },"""

# ---------- 新对象：ODObjs_t 成员 ----------
new_ot = """    OD_obj_record_t o_1804_TPDOCommunicationParameter[6];
    OD_obj_record_t o_1A04_TPDOMappingParameter[9];
    OD_obj_record_t o_2100_epoch_tick[3];
    OD_obj_record_t o_2120_vitals[5];
    OD_obj_record_t o_2130_validity_generation[3];
    OD_obj_record_t o_2140_sqi_probe[3];
    OD_obj_record_t o_2150_nibp_hr_bpstate[3];"""

# ---------- 新对象：ODObjs 初始化 ----------
def rec_odobj_init(name, fields):
    """fields: list of (subIndex, dataOrig, attribute, dataLength)"""
    parts = ["    .%s = {" % name]
    for (s, do, attr, dl) in fields:
        parts.append("        {")
        parts.append("            .dataOrig = %s," % do)
        parts.append("            .subIndex = %d," % s)
        parts.append("            .attribute = %s," % attr)
        parts.append("            .dataLength = %d" % dl)
        parts.append("        },")
    parts.append("    }")
    return "\n".join(parts)

new_odobj_init = []

# 1804 TPDO 通信（PERSIST_COMM）
new_odobj_init.append(rec_odobj_init("o_1804_TPDOCommunicationParameter", [
    (0, "&OD_PERSIST_COMM.x1804_TPDOCommunicationParameter.highestSub_indexSupported", "ODA_SDO_R", 1),
    (1, "&OD_PERSIST_COMM.x1804_TPDOCommunicationParameter.COB_IDUsedByTPDO", "ODA_SDO_RW | ODA_MB", 4),
    (2, "&OD_PERSIST_COMM.x1804_TPDOCommunicationParameter.transmissionType", "ODA_SDO_RW", 1),
    (3, "&OD_PERSIST_COMM.x1804_TPDOCommunicationParameter.inhibitTime", "ODA_SDO_RW | ODA_MB", 2),
    (4, "&OD_PERSIST_COMM.x1804_TPDOCommunicationParameter.eventTimer", "ODA_SDO_RW | ODA_MB", 2),
    (5, "&OD_PERSIST_COMM.x1804_TPDOCommunicationParameter.SYNCStartValue", "ODA_SDO_RW | ODA_MB", 1),
]))
# 1A04 TPDO 映射（PERSIST_COMM）
new_odobj_init.append(rec_odobj_init("o_1A04_TPDOMappingParameter", [
    (0, "&OD_PERSIST_COMM.x1A04_TPDOMappingParameter.numberOfMappedApplicationObjectsInPDO", "ODA_SDO_RW", 1),
    (1, "&OD_PERSIST_COMM.x1A04_TPDOMappingParameter.applicationObject1", "ODA_SDO_RW | ODA_MB", 4),
    (2, "&OD_PERSIST_COMM.x1A04_TPDOMappingParameter.applicationObject2", "ODA_SDO_RW | ODA_MB", 4),
    (3, "&OD_PERSIST_COMM.x1A04_TPDOMappingParameter.applicationObject3", "ODA_SDO_RW | ODA_MB", 4),
    (4, "&OD_PERSIST_COMM.x1A04_TPDOMappingParameter.applicationObject4", "ODA_SDO_RW | ODA_MB", 4),
    (5, "&OD_PERSIST_COMM.x1A04_TPDOMappingParameter.applicationObject5", "ODA_SDO_RW | ODA_MB", 4),
    (6, "&OD_PERSIST_COMM.x1A04_TPDOMappingParameter.applicationObject6", "ODA_SDO_RW | ODA_MB", 4),
    (7, "&OD_PERSIST_COMM.x1A04_TPDOMappingParameter.applicationObject7", "ODA_SDO_RW | ODA_MB", 4),
    (8, "&OD_PERSIST_COMM.x1A04_TPDOMappingParameter.applicationObject8", "ODA_SDO_RW | ODA_MB", 4),
]))
# 2100 epoch/tick（RAM）
new_odobj_init.append(rec_odobj_init("o_2100_epoch_tick", [
    (0, "&OD_RAM.x2100_epoch_tick.highestSub_indexSupported", "ODA_SDO_R", 1),
    (1, "&OD_RAM.x2100_epoch_tick.epoch", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 4),
    (2, "&OD_RAM.x2100_epoch_tick.tick", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 4),
]))
# 2120 vitals（RAM，4 标量均 TPDO 可映射）
new_odobj_init.append(rec_odobj_init("o_2120_vitals", [
    (0, "&OD_RAM.x2120_vitals.highestSub_indexSupported", "ODA_SDO_R", 1),
    (1, "&OD_RAM.x2120_vitals.SpO2", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 2),
    (2, "&OD_RAM.x2120_vitals.PR", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 2),
    (3, "&OD_RAM.x2120_vitals.NIBP_SYS", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 2),
    (4, "&OD_RAM.x2120_vitals.NIBP_DIA", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 2),
]))
# 2130 validity+generation（RAM）
new_odobj_init.append(rec_odobj_init("o_2130_validity_generation", [
    (0, "&OD_RAM.x2130_validity_generation.highestSub_indexSupported", "ODA_SDO_R", 1),
    (1, "&OD_RAM.x2130_validity_generation.data_valid_flags", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 1),
    (2, "&OD_RAM.x2130_validity_generation.boot_generation", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 2),
]))
# 2140 sqi+probe（RAM）
new_odobj_init.append(rec_odobj_init("o_2140_sqi_probe", [
    (0, "&OD_RAM.x2140_sqi_probe.highestSub_indexSupported", "ODA_SDO_R", 1),
    (1, "&OD_RAM.x2140_sqi_probe.signal_quality", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 1),
    (2, "&OD_RAM.x2140_sqi_probe.probe_state", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 1),
]))
# 2150 nibp_hr+bp_state（RAM）
new_odobj_init.append(rec_odobj_init("o_2150_nibp_hr_bpstate", [
    (0, "&OD_RAM.x2150_nibp_hr_bpstate.highestSub_indexSupported", "ODA_SDO_R", 1),
    (1, "&OD_RAM.x2150_nibp_hr_bpstate.nibp_heart_rate", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 1),
    (2, "&OD_RAM.x2150_nibp_hr_bpstate.bp_state", "ODA_SDO_RW | ODA_MB | ODA_TPDO", 1),
]))
new_odobj_init_str = "\n".join(new_odobj_init)

# ---------- 新对象：ODList 条目 ----------
new_odlist = [
    "{0x1804, 0x06, ODT_REC, &ODObjs.o_1804_TPDOCommunicationParameter, NULL},",
    "{0x1A04, 0x09, ODT_REC, &ODObjs.o_1A04_TPDOMappingParameter, NULL},",
    "{0x2100, 0x03, ODT_REC, &ODObjs.o_2100_epoch_tick, NULL},",
    "{0x2120, 0x05, ODT_REC, &ODObjs.o_2120_vitals, NULL},",
    "{0x2130, 0x03, ODT_REC, &ODObjs.o_2130_validity_generation, NULL},",
    "{0x2140, 0x03, ODT_REC, &ODObjs.o_2140_sqi_probe, NULL},",
    "{0x2150, 0x03, ODT_REC, &ODObjs.o_2150_nibp_hr_bpstate, NULL},",
]
# 命名后缀（用于 OD_ENTRY_Hxxxx_name 宏）
new_named = {
    0x1804: "TPDOCommunicationParameter", 0x1A04: "TPDOMappingParameter",
    0x2100: "epoch_tick", 0x2120: "vitals", 0x2130: "validity_generation",
    0x2140: "sqi_probe", 0x2150: "nibp_hr_bpstate",
}

# ============================ 开始改写 OD.h ============================
# 1) OD_CNT_TPDO 4 -> 5
assert "OD_CNT_TPDO 4" in odh, "OD_CNT_TPDO 4 not found"
odh = odh.replace("OD_CNT_TPDO 4", "OD_CNT_TPDO 5", 1)

# 2) 追加 PERSIST_COMM / RAM 结构体成员
assert "} OD_PERSIST_COMM_t;" in odh
odh = odh.replace("} OD_PERSIST_COMM_t;", new_pc_struct + "\n} OD_PERSIST_COMM_t;", 1)
assert "} OD_RAM_t;" in odh
odh = odh.replace("} OD_RAM_t;", new_ram_struct + "\n} OD_RAM_t;", 1)

# 3) 重新生成两段 OD_ENTRY_H 宏（按排序后的索引）
m = re.search(r"(#define OD_ENTRY_H1000 &OD->list\[0\].*?#define OD_ENTRY_H1A03 &OD->list\[32\])\n", odh, re.S)
assert m, "plain OD_ENTRY_H block not found"
plain_block = m.group(1)
assert "} OD_RAM_t;" in odh
# 解析现有 ODList 索引
existing_idx = [int(x, 16) for x in re.findall(r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*0x[0-9A-Fa-f]+\s*,\s*ODT_\w+\s*,", odc)]
all_idx = sorted(set(existing_idx + [0x1804, 0x1A04, 0x2100, 0x2120, 0x2130, 0x2140, 0x2150]))
plain_new = "\n".join("#define OD_ENTRY_H%X &OD->list[%d]" % (i, n) for n, i in enumerate(all_idx))
odh = odh[:m.start()] + plain_new + odh[m.end():]

# 命名宏：解析现有命名映射
named_map = {}
for mm in re.finditer(r"#define OD_ENTRY_H([0-9A-Fa-f]+)_(\w+)\s+&OD->list\[(\d+)\]", odh):
    named_map[int(mm.group(1), 16)] = mm.group(2)
named_map.update(new_named)
m2 = re.search(r"(#define OD_ENTRY_H1000_\w+ &OD->list\[0\].*?#define OD_ENTRY_H1A03_\w+ &OD->list\[32\])\n", odh, re.S)
assert m2, "named OD_ENTRY_H block not found"
named_new = "\n".join("#define OD_ENTRY_H%X_%s &OD->list[%d]" % (i, named_map.get(i, "obj"), n)
                     for n, i in enumerate(all_idx))
odh = odh[:m2.start()] + named_new + odh[m2.end():]

with open(ODH, "w", encoding="utf-8") as f:
    f.write(odh)

# ============================ 开始改写 OD.c ============================
# 4) 修改 8 个既有 TPDO 通信/映射块（1800-1803 / 1A00-1A03）为有效值
for idx in (0x1800, 0x1801, 0x1802, 0x1803):
    header = ".x%X_TPDOCommunicationParameter = {" % idx
    pat = block_pat(header)
    newblk = comm_init(idx)
    odc, n = re.subn(pat, newblk, odc, count=1, flags=re.S)
    assert n == 1, "comm block %X replace failed" % idx
for idx in (0x1A00, 0x1A01, 0x1A02, 0x1A03):
    header = ".x%X_TPDOMappingParameter = {" % idx
    pat = block_pat(header)
    newblk = map_init(idx)
    odc, n = re.subn(pat, newblk, odc, count=1, flags=re.S)
    assert n == 1, "map block %X replace failed" % idx

# 5) 追加 PERSIST_COMM 初始化（1804 + 1A04）到 OD_PERSIST_COMM 初始化末尾（替换 x1A03 块后紧接）
#    通过把 new_pc_init 接到 x1A03 新块后面实现（PERSIST_COMM 初始化顺序无关）
#    定位 x1A03 映射新块，替换为 新块 + new_pc_init
header = ".x1A03_TPDOMappingParameter = {"
pat = block_pat(header)
newblk = map_init(0x1A03) + "\n" + new_pc_init
odc, n = re.subn(pat, newblk, odc, count=1, flags=re.S)
assert n == 1, "x1A03+new pc init replace failed"

# 6) 追加 RAM 初始化（2100-2150）到 OD_RAM 初始化末尾
header = ".x1200_SDOServerParameter = {"
pat = block_pat_struct_end(header)
newblk = (".x1200_SDOServerParameter = {\n"
          "        .highestSub_indexSupported = 0x02,\n"
          "        .COB_IDClientToServerRx = 0x00000600,\n"
          "        .COB_IDServerToClientTx = 0x00000580\n"
          "    },\n" + new_ram_init + "\n};")
# 注意：上面把 x1200 块及其 '};' 替换为 x1200 块 + 新 RAM 初始化 + '};'
odc, n = re.subn(pat, newblk, odc, count=1, flags=re.S)
assert n == 1, "RAM init append failed"

# 7) 追加 ODObjs_t 成员
assert "} ODObjs_t;" in odc
odc = odc.replace("} ODObjs_t;", new_ot + "\n} ODObjs_t;", 1)

# 8) 追加 ODObjs 初始化（7 个新对象）到末尾
#    替换 o_1A03 块最后的 '    }\n};'（ODObjs 初始化结束）
pat = re.escape(".o_1A03_TPDOMappingParameter = {") + r"\s*\{.*?\}\n\};"
# 构造：o_1A03 块（已替换过映射后的）+ 新对象 + '};'
# 简化：直接把 '};\n\n\n/*****    Object dictionary' 之前插入
anchor = "    }\n};"
idx_anchor = odc.rindex(anchor)  # 最后一个 '    }\n};'（ODObjs 初始化结束）
# 把 o_1A03 结束的 '    }' 改为 '    },' 并追加新对象
prefix = odc[:idx_anchor] + "    },"
suffix = odc[idx_anchor + len(anchor):]
odc = prefix + "\n" + new_odobj_init_str + "\n" + suffix

# 9) 重新生成 ODList[]
m3 = re.search(r"(static OD_ATTR_OD OD_entry_t ODList\[\] = \{.*?\{0x0000, 0x00, 0, NULL, NULL\}\n\});", odc, re.S)
assert m3, "ODList block not found"
# 解析现有 ODList 的 (idx, odt, name)
existing_list = re.findall(r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*0x[0-9A-Fa-f]+\s*,\s*(ODT_\w+)\s*,\s*&ODObjs\.(\w+)\s*,", odc)
list_map = {int(x, 16): (odt, name) for (x, odt, name) in existing_list}
list_map.update({
    0x1804: ("ODT_REC", "o_1804_TPDOCommunicationParameter"),
    0x1A04: ("ODT_REC", "o_1A04_TPDOMappingParameter"),
    0x2100: ("ODT_REC", "o_2100_epoch_tick"),
    0x2120: ("ODT_REC", "o_2120_vitals"),
    0x2130: ("ODT_REC", "o_2130_validity_generation"),
    0x2140: ("ODT_REC", "o_2140_sqi_probe"),
    0x2150: ("ODT_REC", "o_2150_nibp_hr_bpstate"),
})
# maxSub 计算：从 all_idx 已算，这里为每条目提供 maxSub
maxsub_map = {
    0x1804: 0x06, 0x1A04: 0x09, 0x2100: 0x03, 0x2120: 0x05,
    0x2130: 0x03, 0x2140: 0x03, 0x2150: 0x03,
}
odlist_new_lines = ["static OD_ATTR_OD OD_entry_t ODList[] = {"]
for i in all_idx:
    odt, name = list_map[i]
    ms = maxsub_map.get(i)
    if ms is None:
        # 从现有 ODList 取 maxSub
        mm = re.search(r"\{\s*0x%X\s*,\s*(0x[0-9A-Fa-f]+)\s*," % i, odc)
        ms = mm.group(1) if mm else "0x01"
    odlist_new_lines.append("    {%s, %s, %s, &ODObjs.%s, NULL}," % ("0x%X" % i, ms, odt, name))
odlist_new_lines.append("    {0x0000, 0x00, 0, NULL, NULL}")
odlist_new_lines.append("};")
odlist_new = "\n".join(odlist_new_lines)
odc = odc[:m3.start()] + odlist_new + odc[m3.end():]

with open(ODC, "w", encoding="utf-8") as f:
    f.write(odc)

print("OD.h / OD.c 扩展完成。新增对象 7 个，ODList 条目数 =", len(all_idx))
print("ODList 排序索引：", [hex(i) for i in all_idx])
