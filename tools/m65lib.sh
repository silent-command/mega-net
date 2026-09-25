# Driving the MEGA65 from a shell script: poll for the change you expect
# instead of sleeping fixed seconds. Source this file. docs/PLATFORM.md
# says what the tools can and cannot do; in short: type lowercase only,
# never a backslash, never a screenshot while an SSH session is running.
#
#   source ../mega-net/tools/m65lib.sh
#   put_d81 bin/FTPC.D81 FTPC.D81          replace a disk in net-tools (resets first)
#   boot_prg ftpc.d81 ftpc 'ost:'          stage, mount, run, wait for the first screen
#   unstage_d81 ftpc.d81                   remove the staged copy when the run is done
#   type_line '192.168.1.232'              a line and RETURN
#   type_keys 'q~M'                        keys, with the m65 escapes
#   wait_for 'entr' 20                     seconds until the word appears, or a message
#   row 23                                 one screen row as text
#   scr                                    the non-blank rows
#
# MEGA65_PORT pins the serial port; the m65 tool is found by name, .osx
# or not.

export MEGA65_PORT="${MEGA65_PORT:-/dev/cu.usbserial-23201}"
if command -v m65.osx >/dev/null 2>&1; then M65=m65.osx; else M65=m65; fi
if command -v mega65_ftp.osx >/dev/null 2>&1; then M65FTP=mega65_ftp.osx; else M65FTP=mega65_ftp; fi

# The screen as text, colour codes stripped. Row N is output line N+2.
raw() { $M65 -S0 2>/dev/null | python3 -c "import sys,re; print(re.sub(r'\x1b\[[0-9;]*m','',sys.stdin.read()))"; }
scr() { raw | python3 -c "import sys; t=sys.stdin.read().split(chr(10)); [print('  |'+l[:79].rstrip()) for l in t[1:51] if l.strip()]"; }
row() { raw | sed -n "$((${1:-23}+2))p" | cut -c1-79; }
type_line() { $M65 -T "$1" >/dev/null 2>&1; }
type_keys() { $M65 -t "$1" >/dev/null 2>&1; }
reset() { $M65 -F >/dev/null 2>&1; }

# wait_for PATTERN [MAXSEC]: polls once a second; case-insensitive, since
# the text screenshot renders a RAM font as the uppercase set.
wait_for() { local t=0 max=${2:-30}; while [ $t -lt $max ]; do raw | grep -qi -- "$1" && { echo "$t"; return 0; }; sleep 1; t=$((t+1)); done; echo "timeout ${max}s waiting for '$1'" >&2; return 1; }
wait_gone() { local t=0 max=${2:-30}; while [ $t -lt $max ]; do raw | grep -qi -- "$1" || { echo "$t"; return 0; }; sleep 1; t=$((t+1)); done; echo "timeout ${max}s waiting for '$1' to go" >&2; return 1; }

# put_d81 FILE NAME: a disk image into net-tools on the card, replacing
# NAME. The disks live in net-tools rather than the root since
# 2026-09-23. mega65_ftp refuses to run with a program in memory, so the
# machine is reset first.
put_d81() { reset; sleep 2; $M65FTP -l "$MEGA65_PORT" -c "cd net-tools" -c "del $2" -c "put $1 $2" 2>&1 | grep -q 'in [0-9]* seconds' && echo "put $2"; }

# stage_d81 NAME / unstage_d81 NAME: a copy of a net-tools disk at the
# root, for the length of one test run.
#
# Every serial call here is bounded (perl alarm): an unbounded
# mega65_ftp against a machine that is off, or a port someone else holds,
# hangs for as long as it is allowed and shows nothing (2026-09-25, and
# the same lesson in 2026-09-22's notes).
#
# NOTHING in the toolchain can mount from a subdirectory: BASIC's MOUNT
# answers FILE NOT FOUND for "net-tools/X.D81" and ignores a CHDIR, and
# mega65_ftp says outright "Mounting of files in subdirectories not yet
# implemented". So the only way to drive a program from a script is to
# put its disk at the root first and take it away afterwards. A human at
# the machine uses the Freezer's browser instead, which does walk
# directories. Measured 2026-09-23 against an empty root, so no root copy
# could have answered in its place.
stage_d81() {
  local tmp="/tmp/m65-stage-$1"
  reset; sleep 2                                   # mega65_ftp stalls with a program in memory, as put_d81 knows; boot_prg resets again anyway
  : > "$tmp"                                       # or a stale copy from an earlier run passes the check below
  perl -e 'alarm 120; exec @ARGV' $M65FTP -l "$MEGA65_PORT" -c "cd net-tools" -c "get $1 $tmp" -c "exit" >/dev/null 2>&1
  [ -s "$tmp" ] || { echo "stage_d81: could not fetch $1 from net-tools (machine off, or port held?)" >&2; return 1; }
  perl -e 'alarm 120; exec @ARGV' $M65FTP -l "$MEGA65_PORT" -c "del $1" -c "put $tmp $1" -c "exit" >/dev/null 2>&1
}
unstage_d81() { reset; sleep 2; perl -e 'alarm 60; exec @ARGV' $M65FTP -l "$MEGA65_PORT" -c "del $1" -c "exit" >/dev/null 2>&1; }

# boot_prg DISK PRG PATTERN [TRIES]: stage the disk at the root, then
# reset, mount, run, and wait up to a minute for PATTERN; the start-up
# stick (about one boot in twenty, mega-net 5.18) is retried. Call
# unstage_d81 DISK when the run is finished, or the copy stays at the
# root. Staging costs one download and one upload, about twelve seconds.
boot_prg() { local try t tries=${4:-4}; stage_d81 "$1" || return 1; for try in $(seq 1 $tries); do reset; wait_for 'READY' 8 >/dev/null; type_line "mount \"$1\""; sleep 1; type_line "run \"$2\""; if t=$(wait_for "$3" 60 2>/dev/null); then echo "booted on try $try (${t}s)"; return 0; fi; echo "try $try stuck at '$(row 23)'"; done; return 1; }
