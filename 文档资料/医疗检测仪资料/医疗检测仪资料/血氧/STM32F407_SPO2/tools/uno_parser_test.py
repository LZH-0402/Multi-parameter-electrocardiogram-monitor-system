#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
UNO 17 字节二进制帧解析器 —— 离线自测
=====================================
镜像 HARDWARE/UNO/uno.c 里的状态机 (S0_SYNC1 ... S7_FOOT2)，
不依赖硬件即可验证“头 0A FA 0A 00 02 + 10 字节负载 + 帧尾 00 0B”解析逻辑：
  - 即使 IR/RED 负载里出现 0x0A / 0xFA 也不会误同步；
  - 帧前垃圾、坏帧、背靠背帧都能正确恢复/切分。

帧格式（共 17 字节）：
  [0]0x0A [1]0xFA [2]0x0A [3]0x00 [4]0x02
  [5..8]  IR  int32 小端
  [9..12] RED int32 小端
  [13]    spo2  uint8  (0=无效)
  [14]    hr    uint8
  [15]    0x00
  [16]    0x0B

运行：python3 uno_parser_test.py
退出码 0 = 全部通过；非 0 = 有失败。
"""

import struct
import sys


class UnoParser:
    """与 uno.c 等价的纯 Python 状态机。"""

    S0_SYNC1, S1_SYNC2, S2_LENL, S3_LENH, S4_TYPE, S5_PAYLOAD, S6_FOOT1, S7_FOOT2 = range(8)

    def __init__(self):
        self.st = self.S0_SYNC1
        self.pl = bytearray(10)
        self.pl_i = 0
        self.foot1 = 0
        self.frames = []  # 收集成功解析的 (ir, red, spo2, hr)

    @staticmethod
    def _rd_i32_le(p):
        return struct.unpack("<i", bytes(p[:4]))[0]

    def feed(self, b):
        st = self.st
        if st == self.S0_SYNC1:
            if b == 0x0A:
                st = self.S1_SYNC2
        elif st == self.S1_SYNC2:
            st = self.S2_LENL if b == 0xFA else self.S0_SYNC1
        elif st == self.S2_LENL:
            st = self.S3_LENH if b == 0x0A else self.S0_SYNC1
        elif st == self.S3_LENH:
            st = self.S4_TYPE if b == 0x00 else self.S0_SYNC1
        elif st == self.S4_TYPE:
            self.pl_i = 0
            st = self.S5_PAYLOAD if b == 0x02 else self.S0_SYNC1
        elif st == self.S5_PAYLOAD:
            self.pl[self.pl_i] = b
            self.pl_i += 1
            if self.pl_i >= 10:
                st = self.S6_FOOT1
        elif st == self.S6_FOOT1:
            self.foot1 = b
            st = self.S7_FOOT2
        elif st == self.S7_FOOT2:
            if self.foot1 == 0x00 and b == 0x0B:
                ir = self._rd_i32_le(self.pl[0:4])
                red = self._rd_i32_le(self.pl[4:8])
                spo2 = self.pl[8]
                hr = self.pl[9]
                self.frames.append((ir, red, spo2, hr))
            st = self.S0_SYNC1
        self.st = st

    def feed_bytes(self, data):
        for b in data:
            self.feed(b)


def build_frame(ir, red, spo2, hr):
    return (bytes([0x0A, 0xFA, 0x0A, 0x00, 0x02])
            + struct.pack("<iiBB", ir, red, spo2, hr)
            + bytes([0x00, 0x0B]))


def to_signed32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v >= 0x80000000 else v


def run():
    fails = 0

    # T1：正常帧（含负数 IR/RED）
    p = UnoParser()
    f = build_frame(to_signed32(-171818032), 305419896, 97, 72)
    p.feed_bytes(f)
    exp = (-171818032, 305419896, 97, 72)
    ok = p.frames == [exp]
    print(("PASS" if ok else "FAIL"), "T1 normal frame ->", p.frames, "expect", [exp])
    fails += 0 if ok else 1

    # T2：负载里内嵌 0A FA（必须不误同步）
    p = UnoParser()
    f = build_frame(302709300, to_signed32(-97252778), 88, 65)
    p.feed_bytes(f)
    exp = (302709300, -97252778, 88, 65)
    ok = p.frames == [exp]
    print(("PASS" if ok else "FAIL"), "T2 embedded-sync payload ->", p.frames, "expect", [exp])
    fails += 0 if ok else 1

    # T3：帧前一堆垃圾，之后一个完整帧
    p = UnoParser()
    garbage = bytes([0x00, 0xFF, 0x0A, 0x0A, 0x0A, 0xFA, 0x00, 0x99, 0x0B])
    p.feed_bytes(garbage + build_frame(123456, to_signed32(-654321), 95, 70))
    exp = (123456, -654321, 95, 70)
    ok = p.frames == [exp]
    print(("PASS" if ok else "FAIL"), "T3 garbage then valid ->", p.frames, "expect", [exp])
    fails += 0 if ok else 1

    # T4：坏帧（帧尾不对）被拒，随后好帧被正确解析
    p = UnoParser()
    bad = bytes([0x0A, 0xFA, 0x0A, 0x00, 0x02]) + struct.pack("<iiBB", -10, -20, 84, 60) + bytes([0x00, 0x00])
    good = build_frame(-10, -20, 84, 60)
    p.feed_bytes(bad + good)
    exp = (-10, -20, 84, 60)
    ok = p.frames == [exp]
    print(("PASS" if ok else "FAIL"), "T4 bad frame rejected, then good ->", p.frames, "expect", [exp])
    fails += 0 if ok else 1

    # T5：两个背靠背帧
    p = UnoParser()
    f1 = build_frame(287454020, 1432778632, 98, 71)
    f2 = build_frame(287454020, 1432778632, 97, 73)
    p.feed_bytes(f1 + f2)
    exp = [(287454020, 1432778632, 98, 71), (287454020, 1432778632, 97, 73)]
    ok = p.frames == exp
    print(("PASS" if ok else "FAIL"), "T5 back-to-back ->", p.frames, "expect", exp)
    fails += 0 if ok else 1

    print("=" * 30)
    if fails == 0:
        print("ALL PARSER TESTS PASSED")
    else:
        print("SOME TESTS FAILED (%d)" % fails)
    return fails


if __name__ == "__main__":
    sys.exit(1 if run() else 0)
