#!/usr/bin/env bash
# Report whether this machine can build, flash, and talk to the STM32 Shift Clock.
# Checks every prerequisite before exiting, so one gap does not hide another.
# Exits non-zero if anything is missing. See docs/DEVELOPMENT.md.

set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TIMESYNC="$REPO/software/timesync"

# USB descriptors, in decimal as ioreg reports them.
VID_ST=1155      # 0x0483 STMicroelectronics
PID_CDC=22336    # 0x5740 board's USB CDC interface
PID_STLINK=14162 # 0x3752 ST-Link debug probe

gaps=0

ok()   { printf '  \033[32mok\033[0m    %s\n' "$1"; }
miss() { printf '  \033[31mMISS\033[0m  %s\n' "$1"; printf '        → %s\n' "$2"; gaps=$((gaps + 1)); }
note() { printf '        %s\n' "$1"; }

# ioreg lives in /usr/sbin; fall back to the absolute path so a trimmed PATH
# cannot turn "cannot look" into a false "not attached".
IOREG="$(command -v ioreg 2>/dev/null || true)"
[ -x "$IOREG" ] || IOREG=/usr/sbin/ioreg

# Is a USB device with this vendor/product id attached?
usb_present() {
  "$IOREG" -r -c IOUSBHostDevice -l -w 0 2>/dev/null |
    awk -v vid="$1" -v pid="$2" '
      /^\+-o/ { v = 0; p = 0 }
      /"idVendor" =/  { n = $NF; gsub(/[^0-9]/, "", n); if (n == vid) v = 1 }
      /"idProduct" =/ { n = $NF; gsub(/[^0-9]/, "", n); if (n == pid) p = 1 }
      v && p { found = 1 }
      END { exit(found ? 0 : 1) }
    '
}

echo "STM32 Shift Display — development environment"
echo
echo "Firmware toolchain"

if command -v pio >/dev/null 2>&1; then
  ok "pio — $(pio --version 2>/dev/null) at $(command -v pio)"
else
  miss "pio — PlatformIO Core not on PATH" "uv tool install platformio"
fi

echo
echo "Host tooling"

if command -v uv >/dev/null 2>&1; then
  ok "uv — $(uv --version 2>/dev/null)"
else
  miss "uv — not on PATH" "curl -LsSf https://astral.sh/uv/install.sh | sh"
fi

if [ -f "$TIMESYNC/uv.lock" ]; then
  if (cd "$TIMESYNC" && uv run --frozen python -c "import serial, ntplib" >/dev/null 2>&1); then
    ok "timesync environment — pyserial and ntplib importable"
  else
    miss "timesync environment — dependencies not installed" \
         "cd software/timesync && uv sync"
  fi
else
  miss "timesync environment — $TIMESYNC/uv.lock missing" \
       "cd software/timesync && uv lock && uv sync"
fi

echo
echo "Hardware"

if [ ! -x "$IOREG" ]; then
  miss "cannot enumerate USB — ioreg not found at $IOREG" \
       "This check needs macOS's ioreg; inspect the devices by hand, or run 'pio device list'."
else
  if usb_present "$VID_ST" "$PID_STLINK"; then
    ok "ST-Link debug probe attached"
  else
    miss "ST-Link debug probe not found" \
         "Connect the ST-Link to the board's SWD header and to this Mac. Flashing needs it; there is no DFU path."
  fi

  if usb_present "$VID_ST" "$PID_CDC"; then
    ok "board USB CDC attached"
    for node in /dev/cu.usbmodem*; do
      [ -e "$node" ] && note "serial port candidate: $node"
    done
  else
    miss "board USB CDC not found" \
         "Connect the board's USB-C port to this Mac, and confirm firmware with CDC enabled is flashed."
  fi
fi

echo
if [ "$gaps" -eq 0 ]; then
  echo "Ready: build with 'pio run -d firmware', flash with 'pio run -d firmware -t upload'."
  exit 0
fi

echo "$gaps prerequisite(s) missing — see the → lines above. Details in docs/DEVELOPMENT.md."
exit 1
