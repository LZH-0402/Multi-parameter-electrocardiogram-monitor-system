#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
HKB-08B+ 血压传感器 直连自检脚本（PC 侧，用于把传感器从板子上"摘出来"独立验证）

用途：把传感器自带 USB 口（或 USB-TTL 小板接传感器的 TTL 的 RX/TX/GND）插到 PC，
      本脚本直接用 115200/8N1 与它对话，判定"传感器本身好没好"。

用法（Windows）：
  <python> sensor_probe.py --list                 # 列出所有串口
  <python> sensor_probe.py COM5                   # 打开 COM5 并自检
  <python> sensor_probe.py COM5 --baud 9600       # 指定波特率

自检流程：
  1) 发 唤醒(0xAA)    FF C0 03 AD AA   -> 期望回 FF C0 03 AD AA
  2) 发 启动测量(0xA0) FF C0 03 A3 A0   -> 期望回 FF C0 05 xx A0 QYH QYL（约 10Hz）
  3) 发 停止测量(0xA1) FF C0 03 A4 A1   -> 期望回 FF C0 03 A4 A1
  收到的字节按 HEX 实时打印，并尝试解析成帧（FF C0 LEN CKSUM CMD DATA...）。
"""
import sys
import time
import argparse

try:
    import serial
    import serial.tools.list_ports as lp
except ImportError:
    print("缺少 pyserial：pip install pyserial")
    sys.exit(1)

FRAME = {
    0xAA: "WAKE(唤醒)",
    0xA8: "SLEEP(休眠)",
    0xA0: "START(启动测量/气压)",
    0xA1: "STOP(停止测量)",
    0xA2: "?? ",
    0xAC: "RESULT(测量结果)",
    0xAD: "ERROR(错误)",
    0xD0: "START_WAVE",
    0xD1: "STOP_WAVE",
    0xD2: "SET_GAIN",
    0xD3: "WAVE(波形点)",
}


def cksum(cmd, data=b""):
    return (3 + len(data) + cmd + sum(data)) & 0xFF


def make(cmd, data=b""):
    n = 3 + len(data)
    return bytes([0xFF, 0xC0, n, cksum(cmd, data), cmd]) + bytes(data)


def hexs(b):
    return " ".join(f"{x:02X}" for x in b)


def parse_frames(buf):
    """从字节流里尽力解出 HKB 帧，返回 [(cmd, data, raw)]。
    帧布局: FF C0 | LEN | CKSUM | CMD | DATA...   总长 = 2 + LEN，DATA 个数 = LEN - 3"""
    out = []
    i = 0
    while i + 2 <= len(buf):
        if buf[i] == 0xFF and buf[i + 1] == 0xC0:
            n = buf[i + 2]                 # LEN
            total = 2 + n                  # 含 2 字节帧头
            if n >= 3 and i + total <= len(buf):
                cs = buf[i + 3]
                cmd = buf[i + 4]
                data = buf[i + 5:i + total]
                calc = (n + cmd + sum(data)) & 0xFF
                ok = (calc == cs)
                out.append((cmd, data, buf[i:i + total], ok))
                i += total
                continue
        i += 1
    return out


def do_probe(port, baud):
    print(f"打开 {port} @ {baud}/8N1 ...")
    try:
        ser = serial.Serial(port, baud, timeout=0.3)
    except Exception as e:
        print(f"打开失败: {e}")
        return
    time.sleep(0.2)
    ser.reset_input_buffer()

    steps = [
        ("唤醒", make(0xAA)),
        ("启动测量", make(0xA0)),
    ]
    all_buf = bytearray()
    for name, frame in steps:
        print(f"\n>>> 发送 {name}: {hexs(frame)}")
        ser.reset_input_buffer()
        ser.write(frame)
        ser.flush()
        t0 = time.time()
        window = 0.8 if name == "唤醒" else 3.0
        got = bytearray()
        while time.time() - t0 < window:
            chunk = ser.read(256)
            if chunk:
                got += chunk
                print("    RX:", hexs(chunk))
        all_buf += got
        if not got:
            print(f"    (无应答)")
        else:
            for cmd, data, raw, ok in parse_frames(got):
                tag = FRAME.get(cmd, "?")
                print(f"    -> 解析: {tag} cmd=0x{cmd:02X} data={hexs(data)} "
                      f"[{'校验OK' if ok else '校验错'}]")
        if name == "启动测量" and got:
            # 有气压帧就主动停一下，避免气泵一直打
            stop = make(0xA1)
            print("\n>>> 发送 停止测量:", hexs(stop))
            ser.write(stop)
            ser.flush()

    ser.close()
    print("\n===== 小结 =====")
    if all_buf:
        print(f"共收到 {len(all_buf)} 字节 -> 传感器【有响应】，链路/供电正常。")
        print("若唤醒回 FF C0 03 AD AA、启动回气压帧，则传感器本体 100% 正常，")
        print("问题在板侧（电平/针脚/接线/供电），请改查 STM32 那一侧。")
    else:
        print("一个字节都没收到 -> 传感器【没说话】：")
        print("  · 确认 USB 线是数据线（不是只能充电的线），模块供电灯是否亮")
        print("  · 确认打开的就是传感器这个 COM 口（别选到蓝牙/虚拟口）")
        print("  · 若用 USB-TTL 小板：确认 RX/TX 交叉、共 GND、115200")
        print("  · 若接的是 RS232 组则本脚本收不到（那是 RS232 电平）")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", help="串口号，如 COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--list", action="store_true", help="只列出串口")
    args = ap.parse_args()

    if args.list or not args.port:
        print("当前串口：")
        for p in lp.comports():
            print(f"  {p.device}\t{p.description}\t{p.hwid}")
        if not args.port:
            print("\n用法: python sensor_probe.py COM5   (先用 --list 看是哪个口)")
            return
    do_probe(args.port, args.baud)


if __name__ == "__main__":
    main()
