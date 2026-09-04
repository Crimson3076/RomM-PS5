#!/usr/bin/env python3
"""Send a compiled ELF payload to a PS5 running etaHEN's elfldr listener.

This targets the standard PS5 homebrew payload-loader protocol: elfldr.elf
(started by etaHEN on a jailbroken console the user already controls) opens
a TCP socket, accepts a raw ELF binary, and executes it in memory. This
script does not perform any jailbreak or exploit itself -- it assumes the
console is already jailbroken and elfldr is already listening.

Usage:
    python3 deploy_payload.py --host 192.168.1.50 payload.elf
    python3 deploy_payload.py --host 192.168.1.50 --port 9021 payload.elf
"""

import argparse
import socket
import sys
from pathlib import Path

DEFAULT_ELFLDR_PORT = 9021
CHUNK_SIZE = 65536


def send_payload(host: str, port: int, payload_path: Path, timeout: float) -> None:
    data = payload_path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise ValueError(f"{payload_path} does not look like an ELF file")

    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(data)
        print(f"Sent {len(data)} bytes to {host}:{port}")

        # elfldr streams the payload's stdout/stderr back over the same
        # socket once it starts running; print whatever arrives until the
        # console closes the connection.
        sock.settimeout(None)
        try:
            while True:
                chunk = sock.recv(CHUNK_SIZE)
                if not chunk:
                    break
                sys.stdout.buffer.write(chunk)
                sys.stdout.flush()
        except (ConnectionResetError, OSError):
            pass


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path, help="Path to the compiled ELF payload")
    parser.add_argument("--host", required=True, help="IP address of the PS5 on your local network")
    parser.add_argument("--port", type=int, default=DEFAULT_ELFLDR_PORT, help=f"elfldr listener port (default {DEFAULT_ELFLDR_PORT})")
    parser.add_argument("--timeout", type=float, default=10.0, help="Connection timeout in seconds")
    args = parser.parse_args()

    if not args.payload.is_file():
        parser.error(f"payload file not found: {args.payload}")

    send_payload(args.host, args.port, args.payload, args.timeout)


if __name__ == "__main__":
    main()
