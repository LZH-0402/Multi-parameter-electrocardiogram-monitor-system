#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_canopen_d1d11.py — Node-A CANopen D1–D11 验收自检脚本
=============================================================

解析一段 CANopen TPDO 报文记录（文本），自动核查 Node-A 上报 Gateway-C 的
D1–D11 可观测条件（另覆盖 NA-11/NA-12 部分项）。纯标准库，无需第三方包。

对象字典 / TPDO 布局（详见《CANopen_协议扩展_NodeA.md》）：
    TPDO1  0x181 -> 2110  ir_wave(int32 LE) + sample_tick(uint32 LE)  8B  eventTimer=8ms  (125Hz)
    TPDO2  0x281 -> 2111  预留，不发
    TPDO3  0x381 -> 2120  SpO2(u16)+PR(u16)+NIBP_SYS(u16)+NIBP_DIA(u16) 8B  eventTimer=1000ms (1Hz)
    TPDO4  0x481 -> 2100  epoch(uint32)+tick(uint32)                   8B  eventTimer=1000ms (1Hz)
    TPDO5  0x681 -> 2130  data_valid_flags(uint32)+boot_generation(u32) 8B  eventTimer=1000ms (1Hz)

支持的日志格式（两种皆接受）：
    1) candump 风格（时间戳可选）：
       (123456.789) can0  181   [8]  00 00 E4 9C 00 00 00 00
    2) 简化风格：
       181 0000E49C00000000
       (也可带前导时间戳：123456.789 181 0000E49C00000000)

用法：
    python verify_canopen_d1d11.py capture.log [--verbose]
    python verify_canopen_d1d11.py --demo          # 生成 2s 合成日志并自检
    python verify_canopen_d1d11.py capture.log --tol-cadence 0.25

退出码：全部 PASS=0；有 FAIL=1；参数/格式错误=2。
"""

import argparse
import re
import struct
import sys
from collections import defaultdict

# ---- 期望的 COB-ID / 数据长度（字节） ---------------------------------------
EXPECTED_COB = {
    0x181: ("TPDO1->2110 波形", 8),
    0x281: ("TPDO2->2111 预留(不应发)", 0),   # 0 表示期望不出现
    0x381: ("TPDO3->2120 标量", 8),
    0x481: ("TPDO4->2100 时钟", 8),
    0x681: ("TPDO5->2130 有效性", 8),
}
VALID_COB_SET = {0x181, 0x281, 0x381, 0x481, 0x681}
# 旧实现里出现过、现在必须消失的 COB-ID / 对象
FORBIDDEN_COB = {0x581}            # 旧第 5 路状态 TPDO（现让给 0x681）
FORBIDDEN_OBJ_HINT = {0x2160, 0x2140, 0x2150}  # 已删除的私有索引（仅作提示，无法从帧直接判，靠长度/COB 间接）

NIBP_STALE_S = 1800.0              # NIBP_STALE_MS = 1800000ms = 30min
EPOCH_MIN = 1_700_000_000          # ~2023-11
EPOCH_MAX = 2_100_000_000          # ~2036-09
IR_FULLSCALE = 1_200_000           # 归一化后满量程≈1e6，留 20% 余量


class Frame:
    __slots__ = ("t", "cob", "data")

    def __init__(self, t, cob, data):
        self.t = t
        self.cob = cob
        self.data = data


# ---- 日志解析 ---------------------------------------------------------------
CANDUMP_RE = re.compile(
    r"\(?(\d+\.?\d*)\)?\s*"          # 可选时间戳
    r"can\d+\s+"
    r"([0-9A-Fa-f]{1,8})\s+"         # COB-ID
    r"\[(\d+)\]\s+"                  # DLC
    r"([0-9A-Fa-f ]+)"               # 字节（空格分隔）
)
SIMPLE_TS_RE = re.compile(
    r"^\(?(\d+\.?\d*)\)?\s+([0-9A-Fa-f]{1,8})\s+([0-9A-Fa-f ]+)$"
)
SIMPLE_RE = re.compile(r"^([0-9A-Fa-f]{1,8})\s+([0-9A-Fa-f ]+)$")


def _hex_to_bytes(s):
    s = s.strip()
    if " " in s:
        parts = s.split()
    else:
        if len(s) % 2 != 0:
            raise ValueError("奇数长度十六进制数据: %s" % s)
        parts = [s[i:i + 2] for i in range(0, len(s), 2)]
    return bytes(int(p, 16) for p in parts)


def parse_log(path):
    frames = []
    bad = 0
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for ln, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = CANDUMP_RE.match(line)
            if m:
                t = float(m.group(1)) if m.group(1) else None
                cob = int(m.group(2), 16)
                data = _hex_to_bytes(m.group(4))
                frames.append(Frame(t, cob, data))
                continue
            m = SIMPLE_TS_RE.match(line)
            if m:
                t = float(m.group(1))
                cob = int(m.group(2), 16)
                data = _hex_to_bytes(m.group(3))
                frames.append(Frame(t, cob, data))
                continue
            m = SIMPLE_RE.match(line)
            if m:
                cob = int(m.group(1), 16)
                data = _hex_to_bytes(m.group(2))
                frames.append(Frame(None, cob, data))
                continue
            bad += 1
    if bad:
        sys.stderr.write("[warn] 跳过 %d 行无法解析的内容\n" % bad)
    return frames


# ---- 小工具 ----------------------------------------------------------------
def u16le(b, off):
    return struct.unpack_from("<H", b, off)[0]

def u32le(b, off):
    return struct.unpack_from("<I", b, off)[0]

def i32le(b, off):
    return struct.unpack_from("<i", b, off)[0]

def median(xs):
    if not xs:
        return None
    s = sorted(xs)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2.0


# ---- 检查收集 --------------------------------------------------------------
class Report:
    def __init__(self):
        self.items = []  # (tag, name, status, detail)

    def add(self, tag, name, status, detail=""):
        self.items.append((tag, name, status, detail))

    def summary(self):
        n_pass = sum(1 for _, _, s, _ in self.items if s == "PASS")
        n_warn = sum(1 for _, _, s, _ in self.items if s == "WARN")
        n_fail = sum(1 for _, _, s, _ in self.items if s == "FAIL")
        n_skip = sum(1 for _, _, s, _ in self.items if s == "SKIP")
        return n_pass, n_warn, n_fail, n_skip


def run_checks(frames, has_ts, tol_cadence):
    rep = Report()
    by_cob = defaultdict(list)
    for fr in frames:
        by_cob[fr.cob].append(fr)

    # ---------- D1: COB-ID 集合 ----------
    seen = set(by_cob.keys())
    forbidden_seen = seen & FORBIDDEN_COB
    unexpected = seen - VALID_COB_SET
    if forbidden_seen or unexpected:
        detail = []
        if forbidden_seen:
            detail.append("出现应消失的旧 COB-ID: %s" % [hex(c) for c in sorted(forbidden_seen)])
        if unexpected:
            detail.append("出现非预期 COB-ID: %s" % [hex(c) for c in sorted(unexpected)])
        rep.add("D1", "私有 OD / TPDO 范围（COB-ID 集合）", "FAIL", "; ".join(detail))
    else:
        rep.add("D1", "私有 OD / TPDO 范围（COB-ID 集合）", "PASS",
                "仅出现 %s，无旧 0x581" % [hex(c) for c in sorted(seen)])

    # ---------- D2/D3/D4/D5: 各 TPDO 数据长度 ----------
    def check_len(cob, expect, tag, name):
        frs = by_cob.get(cob, [])
        if expect == 0:
            if frs:
                rep.add(tag, name, "FAIL", "期望不发送，却收到 %d 帧" % len(frs))
            else:
                rep.add(tag, name, "PASS", "0 帧（符合预留/禁用）")
            return
        if not frs:
            rep.add(tag, name, "WARN", "未捕获到该 COB-ID 帧（可能录制太短或过滤掉了）")
            return
        bad = [len(f.data) for f in frs if len(f.data) != expect]
        if bad:
            rep.add(tag, name, "FAIL", "数据长度应=%dB，异常帧数=%d（长度示例 %s）" %
                    (expect, len(bad), sorted(set(bad))[:5]))
        else:
            rep.add(tag, name, "PASS", "%d 帧均为 %dB" % (len(frs), expect))

    check_len(0x181, 8, "D5", "TPDO1 0x181 -> 2110（ir_wave+sample_tick，8B）")
    check_len(0x281, 0, "D2", "TPDO2 0x281 -> 2111 预留（0 帧）")
    check_len(0x381, 8, "D2", "TPDO3 0x381 -> 2120（4×u16，8B）")
    check_len(0x481, 8, "D3", "TPDO4 0x481 -> 2100（epoch+tick，8B）")
    check_len(0x681, 8, "D4", "TPDO5 0x681 -> 2130（flags+boot_generation，8B）")

    # ---------- D6 / D10: 节奏（需时间戳） ----------
    def check_cadence(cob, target_ms, tol, tag, name):
        frs = sorted(by_cob.get(cob, []), key=lambda f: f.t if f.t is not None else 0)
        if not frs or frs[0].t is None:
            rep.add(tag, name, "SKIP", "无带时间戳的该 COB-ID 帧，跳过节奏检查")
            return
        intervals = [(frs[i].t - frs[i - 1].t) * 1000.0 for i in range(1, len(frs))]
        if not intervals:
            rep.add(tag, name, "SKIP", "该 COB-ID 帧不足 2 帧，无法算节奏")
            return
        med = median(intervals)
        lo, hi = target_ms * (1 - tol), target_ms * (1 + tol)
        if lo <= med <= hi:
            rep.add(tag, name, "PASS", "中位间隔 %.1f ms（目标 %d ms，容差 %.0f%%）" %
                    (med, target_ms, tol * 100))
        else:
            rep.add(tag, name, "FAIL", "中位间隔 %.1f ms，偏离目标 %d ms（容差 %.0f%%）" %
                    (med, target_ms, tol * 100))

    check_cadence(0x181, 8.0, tol_cadence, "D6", "TPDO1 波形节奏 ≈125Hz (8ms)")
    check_cadence(0x381, 1000.0, max(tol_cadence, 0.2), "D10", "TPDO3 标量节奏 =1Hz (1000ms)")
    check_cadence(0x481, 1000.0, max(tol_cadence, 0.2), "D10", "TPDO4 时钟节奏 =1Hz (1000ms)")
    check_cadence(0x681, 1000.0, max(tol_cadence, 0.2), "D10", "TPDO5 有效性节奏 =1Hz (1000ms)")

    # ---------- D7: 2100:01 = UTC epoch 秒 ----------
    f481 = by_cob.get(0x481, [])
    if not f481:
        rep.add("D7", "2100:01 = UTC epoch 秒", "SKIP", "未捕获 0x481 帧")
    else:
        full481 = [f for f in f481 if len(f.data) >= 8]
        if not full481:
            rep.add("D7", "2100:01 = UTC epoch 秒", "FAIL",
                    "0x481 帧长度=%dB（应 8B），疑似旧 2140(2B) 映射" % len(f481[0].data))
        else:
            epochs = [u32le(f.data, 0) for f in full481]
            ticks = [u32le(f.data, 4) for f in full481]
            in_range = all(EPOCH_MIN < e < EPOCH_MAX for e in epochs)
            ep_sorted = sorted(set(epochs))
            diffs = [ep_sorted[i] - ep_sorted[i - 1] for i in range(1, len(ep_sorted))]
            incr_ok = all(0 < d <= 2 for d in diffs) if diffs else True
            if in_range and incr_ok:
                rep.add("D7", "2100:01 = UTC epoch 秒", "PASS",
                        "epoch≈%d（2026 区间），相邻秒级差∈(0,2]" % epochs[0])
            elif not in_range:
                rep.add("D7", "2100:01 = UTC epoch 秒", "FAIL",
                        "epoch 不在合理 Unix 区间（示例 %s），疑似仍是运行秒" % epochs[:3])
            else:
                rep.add("D7", "2100:01 = UTC epoch 秒", "WARN",
                        "epoch 在区间内但递增异常（差 %s）" % diffs[:5])
            # tick 单调递增
            if len(ticks) >= 2 and all(ticks[i] >= ticks[i - 1] for i in range(1, len(ticks))):
                rep.add("D7b", "2100:02 tick 单调（毫秒节拍）", "PASS", "tick 单调递增")
            elif len(ticks) >= 2:
                rep.add("D7b", "2100:02 tick 单调（毫秒节拍）", "WARN", "tick 出现回退（可能 uwTick 回绕或乱序）")

    # ---------- D8: 定点单位 ----------
    f381 = by_cob.get(0x381, [])
    if not f381:
        rep.add("D8", "2120 标量定点单位（SpO2/PR u16/100、NIBP u16/10）", "SKIP", "未捕获 0x381 帧")
    else:
        full381 = [f for f in f381 if len(f.data) >= 8]
        if not full381:
            rep.add("D8", "2120 标量定点单位（SpO2/PR u16/100、NIBP u16/10）", "FAIL",
                    "0x381 帧长度=%dB（应 8B）" % len(f381[0].data))
        else:
            spo2 = [u16le(f.data, 0) / 100.0 for f in full381]
            pr = [u16le(f.data, 2) / 100.0 for f in full381]
            sysb = [u16le(f.data, 4) / 10.0 for f in full381]
            diab = [u16le(f.data, 6) / 10.0 for f in full381]
            ok = (all(50 <= v <= 100 for v in spo2) and
                  all(30 <= v <= 250 for v in pr) and
                  all(40 <= v <= 300 for v in sysb) and
                  all(20 <= v <= 250 for v in diab))
            rep.add("D8", "2120 标量定点单位（SpO2/PR u16/100、NIBP u16/10）",
                    "PASS" if ok else "FAIL",
                    "示例 SpO2=%.1f%% PR=%.1fbpm SYS=%.1f DIA=%.1fmmHg（÷100/÷10 后落在生理区间）" %
                    (spo2[0], pr[0], sysb[0], diab[0]) if ok else
                    "除 100/10 后超出生理区间，疑似未用定点单位")

    f181 = by_cob.get(0x181, [])
    if not f181:
        rep.add("D8b", "2110 PPG 归一化（relative i32/1e6）", "SKIP", "未捕获 0x181 帧")
    else:
        ir = [i32le(f.data, 0) for f in f181 if len(f.data) >= 4]
        if not ir:
            rep.add("D8b", "2110 PPG 归一化（relative i32/1e6）", "FAIL",
                    "0x181 帧长度=%dB（应 8B）" % len(f181[0].data))
        else:
            ok = all(abs(v) <= IR_FULLSCALE for v in ir)
            rep.add("D8b", "2110 PPG 归一化（relative i32/1e6）",
                    "PASS" if ok else "FAIL",
                    "ir_wave 绝对值≤%.0f（满量程≈1e6）；示例 %d" % (IR_FULLSCALE, ir[0]) if ok else
                    "ir_wave 溢出 ±%.0f，疑似未归一化" % IR_FULLSCALE)

    # ---------- D9: 2130:01 位图（bit31=synthetic 必须 0） ----------
    f681 = by_cob.get(0x681, [])
    if not f681:
        rep.add("D9", "2130:01 有效位位图 + synthetic=0", "SKIP", "未捕获 0x681 帧")
    else:
        full681 = [f for f in f681 if len(f.data) >= 4]
        if not full681:
            rep.add("D9", "2130:01 有效位位图 + synthetic=0", "FAIL",
                    "0x681 帧长度=%dB（应 8B），疑似旧 u8+u16(3B)" % len(f681[0].data))
        else:
            flags = [u32le(f.data, 0) for f in full681]
            synthetic = [bool(f & 0x80000000) for f in flags]
            if any(synthetic):
                rep.add("D9", "2130:01 bit31(synthetic)=0", "FAIL",
                        "检测到 synthetic=1 的帧（真实数据必须恒 0）")
            else:
                rep.add("D9", "2130:01 bit31(synthetic)=0", "PASS", "全部帧 synthetic=0")
            # 报告各有效位出现情况（供人工核对与探头/测量状态一致）
            bits = {i: any(f & (1 << i) for f in flags) for i in (0, 1, 2, 3, 4, 5, 6, 7)}
            rep.add("D9b", "2130:01 有效位观测（bit0-7）", "PASS",
                    "bit0(PPG)=%d bit1(SpO2)=%d bit2(PR)=%d bit3(NIBP)=%d bit4(probe)=%d bit5(meas)=%d bit6(stop)=%d bit7(wave)=%d" %
                    (bits[0], bits[1], bits[2], bits[3], bits[4], bits[5], bits[6], bits[7]))

    # ---------- D11: NIBP 有效位(bit3) 时效 ----------
    if not f681 or f681[0].t is None:
        rep.add("D11", "NIBP 有效位(bit3) 时效（≤30min）", "SKIP", "无带时间戳的 0x681 帧，跳过")
    else:
        f681_ok = [f for f in f681 if len(f.data) >= 4]
        if not f681_ok:
            rep.add("D11", "NIBP 有效位(bit3) 时效（≤30min）", "FAIL",
                    "0x681 帧长度=%dB（应 8B），无法解析 bit3" % len(f681[0].data))
        else:
            f681_s = sorted(f681_ok, key=lambda f: f.t)
            max_run = 0.0
            run_start = None
            for f in f681_s:
                bit3 = bool(u32le(f.data, 0) & (1 << 3))
                if bit3:
                    if run_start is None:
                        run_start = f.t
                    max_run = max(max_run, f.t - run_start)
                else:
                    run_start = None
            # 若某次 bit3=1 的连续段超过 NIBP_STALE_S 且无刷新 -> 视为潜在违反
            if max_run > NIBP_STALE_S + 1.0:
                rep.add("D11", "NIBP 有效位(bit3) 时效（≤30min）", "FAIL",
                        "检测到 bit3 连续置位 %.0f s > 30min 且无刷新" % max_run)
            else:
                rep.add("D11", "NIBP 有效位(bit3) 时效（≤30min）", "PASS",
                        "bit3 最长连续置位 %.0f s（≤30min），无超时滞留" % max_run)

    # ---------- NA-11b: boot_generation 不自发递增 ----------
    if f681:
        boots = [u32le(f.data, 4) for f in f681 if len(f.data) >= 8]
        if not boots:
            rep.add("NA-11b", "boot_generation 不自发递增", "FAIL",
                    "0x681 帧长度=%dB（应 8B），无法读取 boot_generation" % len(f681[0].data))
        elif len(set(boots)) <= 1:
            rep.add("NA-11b", "boot_generation 不自发递增", "PASS",
                    "24h 内 boot_generation 恒为 %d（无意外复位）" % boots[0])
        else:
            rep.add("NA-11b", "boot_generation 不自发递增", "WARN",
                    "boot_generation 出现 %d 个不同值（若非人工重启则为异常）" % len(set(boots)))

    return rep


# ---- 演示 / 合成日志 -------------------------------------------------------
def make_demo_lines():
    import math
    lines = []
    t0 = 1_760_486_400.0  # 2026-10-15 附近的 epoch
    n = 250               # 2 秒 @125Hz
    for i in range(n):
        t = t0 + i * 0.008
        ir = int(500000 * math.sin(2 * math.pi * 1.2 * i * 0.008))
        sample_tick = i * 8
        data = struct.pack("<iI", ir, sample_tick)
        lines.append("(%0.3f) can0  181   [8]  %s" % (t, " ".join("%02X" % b for b in data)))
    # 标量 / 时钟 / 有效性 各 2 帧（1Hz）
    for k in range(2):
        t = t0 + k * 1.0
        # 2120: SpO2=98.50%->9850, PR=72.00->7200, SYS=120.5->1205, DIA=80.5->805
        data = struct.pack("<HHHH", 9850, 7200, 1205, 805)
        lines.append("(%0.3f) can0  381   [8]  %s" % (t, " ".join("%02X" % b for b in data)))
        # 2100: epoch + tick
        data = struct.pack("<II", int(t), int((t - t0) * 1000))
        lines.append("(%0.3f) can0  481   [8]  %s" % (t, " ".join("%02X" % b for b in data)))
        # 2130: flags(bit0-3 + bit7) + boot_generation=1, synthetic=0
        flags = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 7)
        data = struct.pack("<II", flags, 1)
        lines.append("(%0.3f) can0  681   [8]  %s" % (t, " ".join("%02X" % b for b in data)))
    return lines


def main():
    ap = argparse.ArgumentParser(description="Node-A CANopen D1-D11 验收自检")
    ap.add_argument("logfile", nargs="?", help="CAN 报文文本日志（candump 或简化风格）")
    ap.add_argument("--demo", action="store_true", help="生成 2 秒合成日志并自检（无需硬件）")
    ap.add_argument("--verbose", action="store_true", help="打印每帧解析细节")
    ap.add_argument("--tol-cadence", type=float, default=0.25,
                    help="节奏容差（默认 0.25 = ±25%%）")
    args = ap.parse_args()

    if args.demo:
        lines = make_demo_lines()
        frames = parse_log_from_lines(lines)
        print("[demo] 已生成 %d 帧合成日志（2s @125Hz 波形 + 1Hz 标量/时钟/有效性）\n" % len(frames))
    elif args.logfile:
        frames = parse_log(args.logfile)
        print("[info] 解析到 %d 帧\n" % len(frames))
    else:
        ap.print_help()
        return 2

    if not frames:
        sys.stderr.write("[error] 未解析到任何帧\n")
        return 2

    has_ts = any(f.t is not None for f in frames)
    rep = run_checks(frames, has_ts, args.tol_cadence)

    print("%-8s %-52s %-5s %s" % ("项", "名称", "结果", "说明"))
    print("-" * 110)
    for tag, name, status, detail in rep.items:
        print("%-8s %-52s %-5s %s" % (tag, name, status, detail))
    npass, nwarn, nfail, nskip = rep.summary()
    print("-" * 110)
    print("汇总: PASS=%d  WARN=%d  FAIL=%d  SKIP=%d" % (npass, nwarn, nfail, nskip))
    if args.verbose:
        print("\n[verbose] 各 COB-ID 帧数:")
        cnt = defaultdict(int)
        for f in frames:
            cnt[f.cob] += 1
        for c in sorted(cnt):
            print("  %s : %d 帧" % (hex(c), cnt[c]))
    return 1 if nfail else 0


def parse_log_from_lines(lines):
    # 与 parse_log 同样的逻辑，但输入是字符串列表（供 --demo 用）
    frames = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        m = CANDUMP_RE.match(line)
        if m:
            t = float(m.group(1)) if m.group(1) else None
            frames.append(Frame(t, int(m.group(2), 16), _hex_to_bytes(m.group(4))))
            continue
        m = SIMPLE_TS_RE.match(line)
        if m:
            frames.append(Frame(float(m.group(1)), int(m.group(2), 16), _hex_to_bytes(m.group(3))))
            continue
        m = SIMPLE_RE.match(line)
        if m:
            frames.append(Frame(None, int(m.group(1), 16), _hex_to_bytes(m.group(2))))
    return frames


if __name__ == "__main__":
    sys.exit(main())
