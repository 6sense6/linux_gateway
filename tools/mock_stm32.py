#!/usr/bin/env python3
# mock_stm32.py: 往 /tmp/ttySTM32 灌协议帧, 扮演STM32发送侧
# 依赖: sudo apt install python3-serial
# 用法:
#   python3 tools/mock_stm32.py          普通模式, 每秒1帧温湿度
#   python3 tools/mock_stm32.py --noise  折磨模式, 每轮先攻击再发好帧

import sys
import time
import struct
import random

import serial

PORT = "/tmp/ttySTM32"
BAUD = 115200


def build_frame(payload):
    """按协议拼帧: AA 55 LEN payload CHECKSUM, 校验为LEN到payload累加取低8位"""
    head = bytes([0xAA, 0x55, len(payload)])
    frame = head + payload
    checksum = sum(frame[2:]) & 0xFF
    return frame + bytes([checksum])


def build_th(i):
    """第i帧温湿度, 数值随i缓慢变化, 方便两端对数"""
    temp = 250 + i % 30          # 25.0 ~ 27.9 度
    humi = 550 + i % 40          # 55.0 ~ 58.9 %
    return build_frame(struct.pack(">hh", temp, humi)), temp, humi


# ---------- 六种攻击, 每种对应一类线路故障, 与单测用例一一呼应 ----------

def atk_half(ser, i):
    """半包: 一帧拆两段写, 中间隔0.2秒"""
    f = build_frame(struct.pack(">hh", 111, 222))
    ser.write(f[:3])
    time.sleep(0.2)
    ser.write(f[3:])


def atk_sticky(ser, i):
    """粘包: 两帧粘成一次写"""
    ser.write(build_frame(struct.pack(">hh", 111, 222)) +
              build_frame(struct.pack(">hh", 333, 444)))


def atk_badck(ser, i):
    """坏校验: 篡改校验字节"""
    f = bytearray(build_frame(struct.pack(">hh", 111, 222)))
    f[-1] ^= 0x01
    ser.write(bytes(f))


def atk_badlen(ser, i):
    """非法LEN: 帧头后跟0xFF"""
    ser.write(bytes([0xAA, 0x55, 0xFF]))


def atk_garbage(ser, i):
    """纯噪声: 随机字节, 刻意避开0xAA, 保证紧跟的好帧不被误伤"""
    junk = bytearray()
    for _ in range(random.randrange(4, 12)):
        b = random.randrange(256)
        if b != 0xAA:
            junk.append(b)
    ser.write(bytes(junk))


def atk_fake_head(ser, i):
    """假帧头: 合法帧, 但payload前3字节就是AA 55 04"""
    ser.write(build_frame(bytes([0xAA, 0x55, 0x04, 0x01])))


ATTACKS = [atk_half, atk_sticky, atk_badck, atk_badlen, atk_garbage, atk_fake_head]


def main():
    noise = "--noise" in sys.argv
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
    except serial.SerialException as e:
        print(f"打开{PORT}失败: {e}")
        print("先确认socat虚拟串口对已经挂着")
        sys.exit(1)

    print(f"mock open {PORT} @ {BAUD}, mode={'noise' if noise else 'normal'}")
    i = 0
    try:
        while True:
            if noise:
                atk = ATTACKS[i % len(ATTACKS)]     # 轮流上, 不随机, 便于对数
                atk(ser, i)
                print(f"[atk  {i:3d}] {atk.__name__}")
                time.sleep(0.3)
            f, temp, humi = build_th(i)
            ser.write(f)
            print(f"[send {i:3d}] temp={temp / 10:.1f}C humi={humi / 10:.1f}% {f.hex(' ')}")
            i += 1
            time.sleep(1)
    except KeyboardInterrupt:
        print("mock exit")
    finally:
        ser.close()


if __name__ == "__main__":
    main()