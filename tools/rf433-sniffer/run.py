# Runs reader commands one after another and shows the output live.
# A running monitor.py lends the port meanwhile.
#   run.py "on" "w0" "levels" "map 00 3F"
# Output also goes to run.log next to this file.
import glob, os, sys, time
import serial

here = os.path.dirname(os.path.abspath(__file__))
flag = os.path.join(here, '.uploading')
log = open(os.path.join(here, 'run.log'), 'a')

def out(t):
    print(t, end='', flush=True)
    log.write(t)
    log.flush()

# How a long command says it is done.
DONE = {'map': ('map finished', 'stopped', "aren't visible"),
        'speed': ('5 frame(s):', "aren't visible"),
        'watch': ('marked',), 'fade': ('fade finished',), 'trace': ('trace finished',), 'levels': ('Hz',), 'stat': ('normal receiver', 'has 12 V')}

def clean(b):
    return ''.join(c for c in b.decode('ascii', 'ignore') if c.isprintable() or c == '\n')

def settle(s):
    """Wait for a fresh, unstuck ESP32. Opening the port often restarts it."""
    for attempt in range(6):
        n, t, text = 0, time.time(), ''
        while time.time() - t < 3.0:
            d = s.read(4096)
            n += len(d)
            text += clean(d)
            if n > 40000:
                break
        if n < 20000:
            return
        s.baudrate = 9600
        s.baudrate = 115200
        s.reset_input_buffer()
        s.dtr = False
        s.rts = True
        time.sleep(0.2)
        s.rts = False
    raise SystemExit('the USB chip is stuck: unplug and replug the ESP32')

open(flag, 'w').close()
try:
    time.sleep(1.5)
    g = glob.glob('/dev/cu.usbserial*')
    if not g:
        raise SystemExit('no ESP32 found')
    s = serial.Serial(g[0], 115200, timeout=0.1)
    s.dtr = False
    s.rts = False
    settle(s)
    out('\n[%s]\n' % time.strftime('%Y-%m-%d %H:%M:%S'))
    for cmd in sys.argv[1:]:
        out('> %s\n' % cmd)
        s.write((cmd + '\n').encode())
        word = cmd.split()[0]
        ends = DONE.get(word)
        limit = 1500 if word in ('map', 'watch') else 90 if word == 'trace' else 150 if word == 'speed' else 12 if word in ('fade', 'hold', 'levels') else 4
        text, t, quiet = '', time.time(), time.time()
        while time.time() - t < limit:
            d = clean(s.read(4096))
            if d:
                out(d)
                text += d
                quiet = time.time()
            if ends and any(e in text for e in ends) and time.time() - quiet > 0.5:
                break
            if not ends and time.time() - t > 1.2 and time.time() - quiet > 0.8:
                break
        else:
            if ends:
                out('[no answer to "%s" in %d s]\n' % (cmd, limit))
    s.close()
finally:
    if os.path.exists(flag):
        os.remove(flag)
