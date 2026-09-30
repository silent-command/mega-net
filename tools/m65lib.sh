# Driving the MEGA65 from a shell script: poll for the change you expect
# instead of sleeping fixed seconds. Source this file. docs/PLATFORM.md
# says what the tools can and cannot do; in short: type lowercase only,
# never a backslash, never a screenshot while an SSH session is running.
#
#   source ../mega-net/tools/m65lib.sh
#   put_d81 bin/FTPC.D81 FTPC.D81          replace a disk at the card root (resets first)
#   boot_prg ftpc.d81 ftpc 'ost:'          mount, run, wait for the first screen
#   type_line '192.168.1.232'              a line and RETURN
#   type_keys 'q~M'                        keys, with the m65 escapes
#   wait_for 'entr' 20                     seconds until the word appears, or a message
#   row 23                                 one screen row as text
#   scr                                    the non-blank rows
#
# MEGA65_PORT pins the serial port; the m65 tool is found by name, .osx
# or not.

export MEGA65_PORT="${MEGA65_PORT:-/dev/cu.usbserial-23201}"

# port_guard: before touching the machine. M65Connect's driver is the
# user's and is never touched: if it holds the port, say so and stop.
# Any other m65/mega65_ftp on this Mac was started by one of these
# scripts; end it, and the shell that would only start another, unless
# that shell is an ancestor of this one. Then check the adapter is not
# wedged (EINVAL on its own attributes), which only a power cycle clears.
port_guard() {
  local p anc="" d pp
  if lsof -t "$MEGA65_PORT" 2>/dev/null | xargs -n1 ps -o command= -p 2>/dev/null | grep -q M65Connect; then
    echo "port_guard: M65Connect holds $MEGA65_PORT; quit it first" >&2; return 2; fi
  p=$$; while [ "$p" -gt 1 ]; do anc=" $p$anc"; p=$(ps -o ppid= -p $p | tr -d ' '); done
  for d in $(pgrep -x m65.osx; pgrep -x m65; pgrep -f 'mega65_ftp(\.osx)?$'); do
    ps -o command= -p $d 2>/dev/null | grep -q M65Connect && continue
    pp=$(ps -o ppid= -p $d | tr -d ' ')
    case "$anc " in *" $pp "*) ;; *) kill -KILL $pp 2>/dev/null && echo "port_guard: ended stale shell $pp";; esac
    kill -TERM $d 2>/dev/null && echo "port_guard: stopped stale driver $d ($(ps -o command= -p $d 2>/dev/null | cut -c1-40))"
  done
  sleep 1
  python3 -c 'import os,sys,termios; f=os.open(sys.argv[1],os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK); termios.tcsetattr(f,termios.TCSANOW,termios.tcgetattr(f))' "$MEGA65_PORT" 2>/dev/null \
    || { echo "port_guard: the adapter is wedged; power-cycle the MEGA65" >&2; return 3; }
}
if command -v m65.osx >/dev/null 2>&1; then M65=m65.osx; else M65=m65; fi
if command -v mega65_ftp.osx >/dev/null 2>&1; then M65FTP=mega65_ftp.osx; else M65FTP=mega65_ftp; fi

# The screen as text, colour codes stripped. Row N is output line N+2.
raw() { perl -e 'alarm 20; exec @ARGV' $M65 -S0 2>/dev/null | python3 -c "import sys,re; print(re.sub(r'\x1b\[[0-9;]*m','',sys.stdin.read()))"; }
scr() { raw | python3 -c "import sys; t=sys.stdin.read().split(chr(10)); [print('  |'+l[:79].rstrip()) for l in t[1:51] if l.strip()]"; }
row() { raw | sed -n "$((${1:-23}+2))p" | cut -c1-79; }
type_line() { perl -e 'alarm 30; exec @ARGV' $M65 -T "$1" >/dev/null 2>&1; }
type_keys() { perl -e 'alarm 30; exec @ARGV' $M65 -t "$1" >/dev/null 2>&1; }
reset() { perl -e 'alarm 20; exec @ARGV' $M65 -F >/dev/null 2>&1; }

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
# minute for PATTERN; the start-up stick (about one boot in twenty,
# mega-net 5.18) is retried. The disk is mounted where it lives, so
# there is no staging and nothing to clean up (stage_d81/unstage_d81
# are gone with the net-tools folder, 2026-09-29).
boot_prg() { local try t tries=${4:-4}; port_guard || return 1; for try in $(seq 1 $tries); do reset; wait_for 'READY' 8 >/dev/null; type_line "mount \"$1\""; sleep 1; type_line "run \"$2\""; if t=$(wait_for "$3" 60 2>/dev/null); then echo "booted on try $try (${t}s)"; return 0; fi; echo "try $try stuck at '$(row 23)'"; done; return 1; }
