# Copyright 2026 Justin Cameron
# SPDX-License-Identifier: Apache-2.0
# Driving the MEGA65 from a shell script: poll for the change you expect
# instead of sleeping fixed seconds. Source this file. docs/PLATFORM.md
# says what the tools can and cannot do; in short: type lowercase only,
# never a backslash, never a screenshot while an SSH session is running.
#
#   source ../mega-net/tools/m65lib.sh
#   put_d81 bin/FTPC.D81 FTPC.D81          replace a disk at the card root (resets first)
#   boot_prg ftpc.d81 ftpc 'ost:'          mount, run, wait for the first screen
#   type_line '192.168.1.251'              a line and RETURN
#   type_keys 'q~M'                        keys, with the m65 escapes
#   wait_for 'entr' 20                     seconds until the word appears, or a message
#   row 23                                 one screen row as text
#   scr                                    the non-blank rows
#
# Where the machine is, as tools/device.py finds it: MEGA65_PORT if set;
# else a USB serial port on this computer (the monitor is the FTDI
# channel ending in 1); else the keyboard bridge, tcp#$MEGA65_BRIDGE
# (default pi3:4510; mega-keybridge/README.md). Every m65 call gets -l,
# since autodiscovery can't find a bridge. The tools are found in
# MEGA65_TOOLS first -- on a Mac, the builds in mega-keybridge/mac/bin,
# as the stock .osx ones hang in tcp# mode -- then by name, .osx or not.

if [ -z "$MEGA65_PORT" ]; then
  for _p in /dev/cu.usbserial-*1 /dev/serial/by-id/*FTDI*if01-port0; do
    [ -e "$_p" ] && { MEGA65_PORT=$_p; break; }
  done
  MEGA65_PORT=${MEGA65_PORT:-tcp#${MEGA65_BRIDGE:-pi3:4510}}
fi
export MEGA65_PORT
case "$MEGA65_PORT" in tcp*|TCP*) M65_TCP=1 ;; *) M65_TCP= ;; esac

_m65tool() {   # _m65tool m65 -> the binary to run
  local n
  for n in "$1.osx" "$1"; do
    [ -n "$MEGA65_TOOLS" ] && [ -x "$MEGA65_TOOLS/$n" ] && { echo "$MEGA65_TOOLS/$n"; return; }
  done
  for n in "$1.osx" "$1"; do command -v "$n" >/dev/null 2>&1 && { echo "$n"; return; }; done
  echo "$1"
}

# port_guard: before touching the machine. M65Connect's driver is the
# user's and is never touched: if it holds the port, say so and stop.
# Any other m65/mega65_ftp on this Mac was started by one of these
# scripts; end it, and the shell that would only start another, unless
# that shell is an ancestor of this one. Then check the adapter is not
# wedged (EINVAL on its own attributes), which only a power cycle clears.
# Through the bridge there is no local adapter: a stale driver would
# hold the bridge's one client slot, so those still go, and the check is
# that the bridge answers at all.
port_guard() {
  local p anc="" d pp
  if [ -z "$M65_TCP" ] && lsof -t "$MEGA65_PORT" 2>/dev/null | xargs -n1 ps -o command= -p 2>/dev/null | grep -q M65Connect; then
    echo "port_guard: M65Connect holds $MEGA65_PORT; quit it first" >&2; return 2; fi
  p=$$; while [ "$p" -gt 1 ]; do anc=" $p$anc"; p=$(ps -o ppid= -p $p | tr -d ' '); done
  for d in $(pgrep -x m65.osx; pgrep -x m65; pgrep -f 'mega65_ftp(\.osx)?$'); do
    ps -o command= -p $d 2>/dev/null | grep -q M65Connect && continue
    pp=$(ps -o ppid= -p $d | tr -d ' ')
    case "$anc " in *" $pp "*) ;; *) kill -KILL $pp 2>/dev/null && echo "port_guard: ended stale shell $pp";; esac
    kill -TERM $d 2>/dev/null && echo "port_guard: stopped stale driver $d ($(ps -o command= -p $d 2>/dev/null | cut -c1-40))"
  done
  sleep 1
  if [ -n "$M65_TCP" ]; then
    local hp=${MEGA65_PORT#*#}
    nc -z -w 3 "${hp%:*}" "${hp##*:}" >/dev/null 2>&1 \
      || { echo "port_guard: no keyboard bridge at $hp (is the Pi up?)" >&2; return 3; }
    return 0
  fi
  python3 -c 'import os,sys,termios; f=os.open(sys.argv[1],os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK); termios.tcsetattr(f,termios.TCSANOW,termios.tcgetattr(f))' "$MEGA65_PORT" 2>/dev/null \
    || { echo "port_guard: the adapter is wedged; power-cycle the MEGA65" >&2; return 3; }
}
M65=$(_m65tool m65)
M65FTP=$(_m65tool mega65_ftp)

# The screen as text, colour codes stripped. Row N is output line N+2.
raw() { perl -e 'alarm 20; exec @ARGV' $M65 -l "$MEGA65_PORT" -S0 2>/dev/null | python3 -c "import sys,re; print(re.sub(r'\x1b\[[0-9;]*m','',sys.stdin.read()))"; }
scr() { raw | python3 -c "import sys; t=sys.stdin.read().split(chr(10)); [print('  |'+l[:79].rstrip()) for l in t[1:51] if l.strip()]"; }
row() { raw | sed -n "$((${1:-23}+2))p" | cut -c1-79; }
type_line() { perl -e 'alarm 30; exec @ARGV' $M65 -l "$MEGA65_PORT" -T "$1" >/dev/null 2>&1; }
type_keys() { perl -e 'alarm 30; exec @ARGV' $M65 -l "$MEGA65_PORT" -t "$1" >/dev/null 2>&1; }
reset() { perl -e 'alarm 20; exec @ARGV' $M65 -l "$MEGA65_PORT" -F >/dev/null 2>&1; }

# wait_for PATTERN [MAXSEC]: polls once a second; case-insensitive, since
# the text screenshot renders a RAM font as the uppercase set.
wait_for() { local t=0 max=${2:-30}; while [ $t -lt $max ]; do raw | grep -qi -- "$1" && { echo "$t"; return 0; }; sleep 1; t=$((t+1)); done; echo "timeout ${max}s waiting for '$1'" >&2; return 1; }
wait_gone() { local t=0 max=${2:-30}; while [ $t -lt $max ]; do raw | grep -qi -- "$1" || { echo "$t"; return 0; }; sleep 1; t=$((t+1)); done; echo "timeout ${max}s waiting for '$1' to go" >&2; return 1; }

# put_d81 FILE NAME: a disk image onto the card, replacing NAME. The
# disks live at the card's ROOT since 2026-09-29 (the user's call):
# BASIC's MOUNT reaches them directly, and the net-tools folder, which
# mega65_ftp could no longer create new files in, is retired. The del
# runs in its own session (a del and a put in one stalled the card,
# 2026-09-29), and a put onto a zero-byte leftover stalls too, which
# the del clears. mega65_ftp refuses to run with a program in memory,
# so the machine is reset before each session.
put_d81() { port_guard || return 1; reset; sleep 2; perl -e 'alarm 60; exec @ARGV' $M65FTP -l "$MEGA65_PORT" -c "del $2" -c "exit" >/dev/null 2>&1; reset; sleep 2; perl -e 'alarm 300; exec @ARGV' $M65FTP -l "$MEGA65_PORT" -c "put $1 $2" 2>&1 | grep -q 'in [0-9]* seconds' && echo "put $2"; }

# boot_prg DISK PRG PATTERN [TRIES]: reset, mount, run, and wait up to a
# minute for PATTERN, retrying a boot that sticks (the start-up crash
# that mega-net 0.5 fixed, 5.20; the retry stays as a guard). The disk is mounted where it lives, so
# there is no staging and nothing to clean up (stage_d81/unstage_d81
# are gone with the net-tools folder, 2026-09-29).
boot_prg() { local try t tries=${4:-4}; port_guard || return 1; for try in $(seq 1 $tries); do reset; wait_for 'READY' 8 >/dev/null; type_line "mount \"$1\""; sleep 1; type_line "run \"$2\""; if t=$(wait_for "$3" 60 2>/dev/null); then echo "booted on try $try (${t}s)"; return 0; fi; echo "try $try stuck at '$(row 23)'"; done; return 1; }
