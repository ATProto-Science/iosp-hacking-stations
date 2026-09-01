#!/usr/bin/env python3
"""Station 5: Arduino UNO serial -> relay TCP bridge, for a Raspberry Pi.

Replaces the original OpenWrt-router-plus-socat bridge plan (see
../../README.md's "Arduino UNO + OpenWrt bridge" section) — abandoned after
hitting two real hardware dead ends in one session: the Linksys WRT54G
earmarked for it has no USB port at all, and the D-Link DIR-505 (Superglue)
dongle that does have one was locked out (custom WiFi/admin credentials,
reset button behavior unconfirmed). A Raspberry Pi sidesteps both — it's
already the hardware station-2 assumes ("Raspberry Pi + sensor", per that
station's own README), guaranteed to have working USB-serial drivers
(ftdi_sio/ch341/cp210x all ship in a standard Raspberry Pi OS image, no
brltty-style surprises to debug), and can just run this directly instead of
needing a separate serial<->TCP tool (socat/ser2net) at all.

Reads the UNO's serial port line by line (same wire format every other
device in this station speaks — see synth_relay.py's parse_line()) and
forwards each line as-is to the relay's TCP_PORT over the network. Doesn't
touch ATProto itself — same "only the relay talks to ATProto" shape as
every other device here.

Setup on the Pi:
    pip install pyserial
    python3 uno_serial_bridge.py --serial-port /dev/ttyUSB0 --relay-host 192.168.1.15

Usage:
    python3 uno_serial_bridge.py --serial-port /dev/ttyACM0 --relay-host <relay-ip> [--relay-port 8477] [--baud 115200]
"""

import argparse
import socket
import time

import serial


def bridge(serial_port, baud, relay_host, relay_port):
    print(f"[uno-bridge] opening {serial_port} @ {baud}")
    ser = serial.Serial(serial_port, baud, timeout=1)

    while True:
        line = ser.readline()
        if not line:
            continue
        text = line.decode("utf-8", errors="ignore").strip()
        if not text:
            continue

        try:
            with socket.create_connection((relay_host, relay_port), timeout=3) as sock:
                sock.sendall((text + "\n").encode("utf-8"))
            print(f"[uno-bridge] forwarded: {text}")
        except OSError as exc:
            print(f"[uno-bridge] relay send failed ({exc}), dropping this line and continuing")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial-port", required=True, help="e.g. /dev/ttyUSB0 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--relay-host", required=True, help="IP of the machine running synth_relay.py")
    parser.add_argument("--relay-port", type=int, default=8477, help="synth_relay.py's SYNTH_TCP_PORT")
    args = parser.parse_args()

    while True:
        try:
            bridge(args.serial_port, args.baud, args.relay_host, args.relay_port)
        except serial.SerialException as exc:
            print(f"[uno-bridge] serial error ({exc}), retrying in 3s")
            time.sleep(3)


if __name__ == "__main__":
    main()
