#!/usr/bin/env python3
"""Talk to the MEGA65 serial monitor directly (no pyserial needed).

    python3 tools/mon.py r            registers: PC, A, X, Y, Z, B, SP, MAPL/MAPH...
    python3 tools/mon.py 'd 2000'     disassemble
    python3 tools/mon.py 'm 1600'     memory
    python3 tools/mon.py t1 / t0      halt / resume the CPU

Any monitor command is passed through; the reply is printed. The port
is /dev/cu.usbserial-* unless MEGA65_PORT is set. macOS needs the
IOSSIOSPEED ioctl for 2,000,000 baud; Linux uses termios directly;
Windows uses pyserial (pip install pyserial) and MEGA65_PORT=COMn.
REQUIREMENTS.md 5.11."""
import glob, os, struct, sys, time
try:
    import fcntl, termios                    # Unix: no dependencies at all
except ImportError:
    fcntl = termios = None                   # Windows: pyserial does the port

class _PySerialPort:
    """The port on Windows (or anywhere pyserial is preferred): the same
    two operations the Unix file descriptor gives us."""
    def __init__(self, port):
        import serial                        # pip install pyserial
        self.s = serial.Serial(port, 2000000, timeout=0)
    def write(self, data): self.s.write(data)
    def read(self, n): return self.s.read(n)

def open_port():
    port = os.environ.get("MEGA65_PORT") or (glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/ttyUSB*") + [None])[0]
    if not port:
        sys.exit("no serial port found; set MEGA65_PORT (COM5 on Windows)")
    if termios is None:
        try:
            return _PySerialPort(port)
        except ImportError:
            sys.exit("Windows needs pyserial for the monitor: pip install pyserial")
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0; attrs[1] = 0                       # iflag, oflag: raw
    # keep the port's own control flags (a zeroed cflag is baud 0, which
    # hangs the line up on macOS); just force 8N1, no flow control
    attrs[2] = (attrs[2] & ~(termios.PARENB | termios.CSTOPB | termios.CSIZE | termios.CRTSCTS)) \
               | termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0                                     # lflag: raw
    attrs[6][termios.VMIN] = 0; attrs[6][termios.VTIME] = 0
    if sys.platform == "darwin":
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        fcntl.ioctl(fd, 0x80045402, struct.pack("Q", 2000000))   # IOSSIOSPEED
    else:
        attrs[4] = attrs[5] = getattr(termios, "B2000000", 0)
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd

def command(fd, cmd, wait=0.4, gap=0.15):
    """Sends cmd; returns what arrives until `gap` seconds of silence
    (or `wait` seconds with nothing at all)."""
    write = fd.write if isinstance(fd, _PySerialPort) else (lambda b: os.write(fd, b))
    read = fd.read if isinstance(fd, _PySerialPort) else (lambda n: os.read(fd, n))
    write((cmd + "\r").encode())
    out = b""
    end = time.time() + wait
    while time.time() < end:
        try:
            chunk = read(4096)
        except BlockingIOError:
            chunk = b""
        if chunk:
            out += chunk; end = time.time() + gap
        else:
            time.sleep(0.003)
    return out.decode("latin-1")

if __name__ == "__main__":
    fd = open_port()
    for c in (sys.argv[1:] or ["r"]):
        print(command(fd, c).strip())
