#!/usr/bin/env python3
import socket
import sys

HOST = "127.0.0.1"
PORT = 9000 if len(sys.argv) < 2 else int(sys.argv[1])

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # 重启服务端不报"地址被占用"
s.bind((HOST, PORT))
s.listen(1)
print(f"[server] listening on {HOST}:{PORT} ...")

conn, addr = s.accept()
print(f"[server] client {addr} connected")
with conn:
    while True:
        data = conn.recv(4096)
        if not data:
            print("[server] client disconnected")
            break
        sys.stdout.write(data.decode("utf-8", errors="replace"))
        sys.stdout.flush()