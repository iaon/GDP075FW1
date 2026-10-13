#!/usr/bin/env python3
"""Send the recovered vendor UART configuration or a packed TCP frame."""

import argparse
import socket
from pathlib import Path


def provisioning_packet(ssid: str, password: str, host: int) -> bytes:
    name, secret = ssid.encode("utf-8"), password.encode("utf-8")
    if not 1 <= len(name) <= 29 or len(secret) > 29 or b"\0" in name + secret:
        raise ValueError("vendor USB fields allow 1..29 SSID bytes and 0..29 password bytes")
    if not 1 <= host <= 254:
        raise ValueError("host_number must be 1..254")
    packet = bytearray(64)
    packet[0] = len(name)
    packet[1:1 + len(name)] = name
    packet[30] = len(secret)
    packet[31:31 + len(secret)] = secret
    packet[60:64] = bytes((1, host, 13, 10))
    return bytes(packet)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    wifi = commands.add_parser("wifi", help="send the vendor's 64-byte UART packet")
    wifi.add_argument("--port", required=True)
    wifi.add_argument("--ssid", required=True)
    wifi.add_argument("--password", required=True)
    wifi.add_argument("--host", type=int, default=208)
    frame = commands.add_parser("frame", help="send 96000 packed pixel bytes to TCP port 8000")
    frame.add_argument("ip")
    frame.add_argument("file", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "wifi":
            import serial
            packet = provisioning_packet(args.ssid, args.password, args.host)
            # Avoid intentionally resetting the board when opening its UART.
            with serial.Serial(baudrate=115200, timeout=1, write_timeout=5) as port:
                port.dtr = port.rts = False
                port.port = args.port
                port.open()
                if port.write(packet) != len(packet):
                    raise OSError("incomplete UART write")
                port.flush()
            print("Sent Wi-Fi configuration; device saves it and restarts.")
        else:
            data = args.file.read_bytes()
            if len(data) != 96000:
                raise ValueError("frame must contain exactly 96000 bytes")
            with socket.create_connection((args.ip, 8000), timeout=30) as connection:
                connection.sendall(data)
                connection.shutdown(socket.SHUT_WR)
            print("Sent frame and closed connection for display refresh.")
    except (ValueError, OSError, ImportError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
