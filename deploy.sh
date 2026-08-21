#!/usr/bin/env bash
# One-command wrapper: rebuild (Docker/devkitA64), push the launcher .nro to
# a Switch via netloader, then stream its live nxlink log -- no devkitPro/
# pacman needed on the host, see tools/deploy.py for the pure-stdlib
# implementation and its protocol caveats.
#
#   ./deploy.sh <SWITCH_IP>
#   SWITCH_IP=192.168.1.50 ./deploy.sh
#
# Anything after the IP is forwarded to tools/deploy.py as-is, e.g.:
#   ./deploy.sh 192.168.1.50 --nro wine-nx-runtime.nro --no-push
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ $# -lt 1 ] && [ -z "${SWITCH_IP:-}" ]; then
    echo "Usage: $0 <SWITCH_IP> [tools/deploy.py options]" >&2
    echo "   or: SWITCH_IP=<ip> $0 [tools/deploy.py options]" >&2
    exit 1
fi

exec python3 "$SCRIPT_DIR/tools/deploy.py" --rebuild "$@"
