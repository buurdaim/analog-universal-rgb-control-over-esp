# Live view of the RF reader. Reconnects by itself when USB drops.
# Type a button name or a command (stat, list, raw ...) and press Enter.
# Everything is also written to capture.log next to this file.
import glob, os, select, sys, time
import serial

log = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'capture.log'), 'a')

PAUSE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.uploading')

def out(t):
    print(t, end='', flush=True)
    log.write(t)
    log.flush()

def stamp():
    return time.strftime('%H:%M:%S')

def rate(s, secs):
    """Bytes per second arriving right now."""
    n, t = 0, time.time()
    while time.time() - t < secs:
        n += len(s.read(4096))
    return n / secs

def unstick(s):
    out('\n[%s USB chip stuck, clearing it]\n' % stamp())
    for attempt in range(5):
        s.baudrate = 9600
        s.baudrate = 115200
        s.reset_output_buffer()
        s.reset_input_buffer()
        s.dtr = False                    # reset the ESP32: its boot text
        s.rts = True                     # replaces the stuck packet
        time.sleep(0.2)
        s.rts = False
        rate(s, 0.7)
        if rate(s, 0.5) < 12000:
            out('[%s cleared, the ESP32 was restarted]\n' % stamp())
            return
    out('[%s still stuck: unplug and replug the ESP32]\n' % stamp())
    raise OSError

s = None
since, count = 0, 0
out('\n[%s waiting for the ESP32 ...]\n' % stamp())
while True:
    try:
        # upload.sh asks for the port with this file and removes it when done.
        if os.path.exists(PAUSE):
            if s is not None:
                s.close()
                s = None
                out('\n[%s port lent to the upload]\n' % stamp())
            time.sleep(0.3)
            continue
        if s is None:
            g = glob.glob('/dev/cu.usbserial*')
            if not g:
                time.sleep(0.3)
                continue
            s = serial.Serial(g[0], 115200, timeout=0.1)
            out('[%s connected: %s]\n' % (stamp(), g[0]))
            since, count = time.time(), 0
        d = s.read(4096)
        # The USB chip sometimes replays its last packet endlessly, far faster
        # than 115200 baud can deliver. Reset the ESP32 and throw the flood away.
        now = time.time()
        if now - since > 0.5:
            since, count = now, 0
        count += len(d)
        if count > 12000:
            unstick(s)
            since, count = time.time(), 0
            continue
        if d:
            t = d.decode('ascii', 'ignore')
            out(''.join(c for c in t if c.isprintable() or c == '\n'))
        if select.select([sys.stdin], [], [], 0)[0]:
            line = sys.stdin.readline()
            if line == '':
                break
            s.write(line.encode())
    except KeyboardInterrupt:
        break
    except (OSError, serial.SerialException):
        out('\n[%s USB dropped]\n' % stamp())
        try:
            s.close()
        except Exception:
            pass
        s = None
        time.sleep(0.5)
