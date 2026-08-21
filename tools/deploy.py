#!/usr/bin/env python3
"""
tools/deploy.py -- zero-dependency Switch deploy + live-log tool for wine-nx.

Pure Python 3 standard library only (socket, struct, subprocess, os, sys,
time, argparse) -- no devkitPro/pacman/nxlink binary needed on the host.
Two independent pieces, deliberately kept separable:

  1. NRO push (port 28280): sends a built .nro to hbmenu's netloader so it
     launches it, without touching the console by hand.

     PROTOCOL CAVEAT, stated up front rather than buried: this is
     implemented from general knowledge of the Switch homebrew netloader
     wire format (a 4-byte big-endian length prefix followed by the raw
     .nro bytes over one TCP connection to port 28280), NOT verified
     against nx-hbmenu's own netloader.c source or a real packet capture
     of the official `nxlink` tool. If the push step connects but hbmenu
     never reacts, or the connection hangs, that is the first thing to
     suspect -- cross-check against
     https://github.com/switchbrew/nx-hbmenu (netloader.c), or capture
     real `nxlink` traffic with Wireshark, before assuming anything else
     (network, firewall, IP) is wrong. Also: "Enable Netloader" has to be
     turned on in hbmenu on the console itself first, and hbmenu has to
     actually be the thing running (not some other app) -- there is no
     way to automate either of those from the host.

  2. Log streaming (port 28771): confirmed directly against libnx's own
     nxlink.h, not assumed -- nxlinkStdio() is a CLIENT, it connects OUT
     to a host IP (__nxlink_host) and redirects stdout/stderr onto that
     socket. It never listens on this port itself. So this side is the
     server: this script binds :28771 and waits for the target to
     connect in. For the target to have anywhere to connect to, the SD
     card needs sdmc:/switch/wine/nxlink-host.txt containing this
     machine's own LAN IP -- nxlinkStdio()'s usual auto-discovery
     (__nxlink_host populated by the real nxlink tool's own netloader
     push) never fires for a plain hbmenu launch. See
     wine_nx_nxlink_connect() (wine-nx-probe/source/runtime.c) for the
     target-side half of this.

Usage:
    SWITCH_IP=192.168.1.50 tools/deploy.py
    tools/deploy.py 192.168.1.50 --rebuild
    tools/deploy.py 192.168.1.50 --nro wine-nx-runtime.nro
    tools/deploy.py 192.168.1.50 --no-push          # just stream logs
    tools/deploy.py 192.168.1.50 --no-log           # just push, don't stream
"""
import argparse
import os
import socket
import struct
import subprocess
import sys
import time

NETLOADER_PORT = 28280
NXLINK_STDIO_PORT = 28771

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_SCRIPT = os.path.join(REPO_ROOT, "wine-nx-probe", "build-switch.sh")
STAGED_SD_DIR = os.path.join(REPO_ROOT, "wine-nx-probe", "build-switch", "sd-card", "switch", "wine")


def resolve_switch_ip(cli_ip):
    ip = cli_ip or os.environ.get("SWITCH_IP")
    if not ip:
        sys.exit("No Switch IP given -- pass it as an argument or set $SWITCH_IP.")
    return ip


def run_rebuild(app_kind):
    """Runs wine-nx-probe/build-switch.sh (the existing Docker/devkitA64
    build) unmodified -- this script only ever calls it, never
    reimplements any part of it."""
    if not os.path.isfile(BUILD_SCRIPT):
        sys.exit(f"[deploy] build script not found: {BUILD_SCRIPT}")
    env = os.environ.copy()
    if app_kind:
        env["WINE_NX_APP"] = app_kind
    print(f"[deploy] rebuilding via {BUILD_SCRIPT} (Docker/devkitA64 build) ...")
    result = subprocess.run([BUILD_SCRIPT], cwd=REPO_ROOT, env=env)
    if result.returncode != 0:
        sys.exit(f"[deploy] build-switch.sh failed (exit {result.returncode}) -- aborting deploy")
    print("[deploy] build finished")


def find_nro(name):
    """`name` is either a bare filename (looked up in the staged sd-card
    directory build-switch.sh already produces) or a path of its own."""
    path = name if (os.path.isabs(name) or os.sep in name) else os.path.join(STAGED_SD_DIR, name)
    if not os.path.isfile(path):
        sys.exit(f"[deploy] .nro not found: {path}\n"
                  f"         (run with --rebuild first, or point --nro at an existing file)")
    return path


def push_nro(ip, nro_path, timeout):
    """Sends `nro_path` to the target's netloader listener. See this
    file's own module docstring for the protocol caveat."""
    size = os.path.getsize(nro_path)
    print(f"[deploy] connecting to {ip}:{NETLOADER_PORT} to push "
          f"{os.path.basename(nro_path)} ({size} bytes) ...")
    try:
        with socket.create_connection((ip, NETLOADER_PORT), timeout=timeout) as sock:
            sock.sendall(struct.pack(">I", size))
            sent = 0
            with open(nro_path, "rb") as f:
                while True:
                    chunk = f.read(64 * 1024)
                    if not chunk:
                        break
                    sock.sendall(chunk)
                    sent += len(chunk)
        print(f"[deploy] sent {sent} bytes -- waiting for the target to launch it")
    except OSError as exc:
        sys.exit(f"[deploy] could not reach {ip}:{NETLOADER_PORT}: {exc}\n"
                  f"         (is hbmenu running with 'Enable Netloader' turned on?)")


def stream_logs(ip, connect_timeout, retry_interval=1.0):
    """libnx's nxlinkStdio() is a CLIENT: it connects OUT to a host IP
    (__nxlink_host) and redirects stdout/stderr onto that socket -- the
    target never listens on NXLINK_STDIO_PORT itself (confirmed directly
    against libnx's own nxlink.h, not assumed). So this side has to be
    the server: listen on NXLINK_STDIO_PORT, accept the target's
    connection, and print whatever it sends, forever, until interrupted.
    `ip` is only used for the log line here -- nothing about listen()
    itself is target-specific. Waits up to `connect_timeout` seconds for
    the target to actually connect (it needs a moment to boot and reach
    its own wine_nx_nxlink_connect() call).

    For the target to have anywhere to connect to at all,
    sdmc:/switch/wine/nxlink-host.txt on the SD card needs to contain
    this machine's own LAN IP -- see wine_nx_nxlink_connect()
    (wine-nx-probe/source/runtime.c) for why nxlinkStdio()'s own
    netloader-push auto-discovery doesn't apply to a plain hbmenu
    launch."""
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("0.0.0.0", NXLINK_STDIO_PORT))
    listener.listen(1)
    listener.settimeout(connect_timeout)

    print(f"[deploy] listening on :{NXLINK_STDIO_PORT} for {ip} to connect "
          f"(up to {connect_timeout:.0f}s) -- make sure nxlink-host.txt on "
          f"the SD card has this machine's LAN IP")
    try:
        sock, peer = listener.accept()
    except socket.timeout:
        sys.exit(f"[deploy] no connection from the target within "
                  f"{connect_timeout:.0f}s -- is it actually running, past "
                  f"its wine_nx_nxlink_connect() call, and does "
                  f"nxlink-host.txt point at this machine?")
    finally:
        listener.close()

    print(f"[deploy] {peer[0]} connected -- streaming logs (Ctrl+C to stop)")
    print("-" * 70)
    try:
        with sock:
            while True:
                data = sock.recv(4096)
                if not data:
                    print("-" * 70)
                    print("[deploy] connection closed by target")
                    break
                sys.stdout.buffer.write(data)
                sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n[deploy] stopped")


def main():
    parser = argparse.ArgumentParser(
        description="Deploy a wine-nx .nro to a Switch and stream its live "
                     "log -- no host devkitPro/pacman dependency.")
    parser.add_argument("ip", nargs="?", help="Switch IP address (default: $SWITCH_IP)")
    parser.add_argument("--rebuild", action="store_true",
                         help="run wine-nx-probe/build-switch.sh before deploying")
    parser.add_argument("--app", default=None,
                         help="WINE_NX_APP value passed to build-switch.sh when --rebuild is used")
    parser.add_argument("--nro", default="wine-nx-launcher.nro",
                         help="which built .nro to push -- a name in the staged sd-card "
                              "dir, or a full path (default: wine-nx-launcher.nro)")
    parser.add_argument("--no-push", action="store_true",
                         help="skip the netloader push, only stream logs from an already-running target")
    parser.add_argument("--no-log", action="store_true",
                         help="skip log streaming, only push the .nro")
    parser.add_argument("--push-timeout", type=float, default=10.0,
                         help="seconds to wait for the netloader TCP connection (default: 10)")
    parser.add_argument("--log-timeout", type=float, default=30.0,
                         help="seconds to wait for the target's nxlink log connection (default: 30)")
    args = parser.parse_args()

    ip = resolve_switch_ip(args.ip)

    if args.rebuild:
        run_rebuild(args.app)

    if not args.no_push:
        push_nro(ip, find_nro(args.nro), timeout=args.push_timeout)

    if not args.no_log:
        stream_logs(ip, connect_timeout=args.log_timeout)


if __name__ == "__main__":
    main()
