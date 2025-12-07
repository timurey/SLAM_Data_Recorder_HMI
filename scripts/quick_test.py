import serial
import sys
import time

if len(sys.argv) < 2:
    print('Usage: python quick_test.py <PORT>')
    sys.exit(1)

port = sys.argv[1]
try:
    ser = serial.Serial(port, 115200, timeout=1)
    time.sleep(0.2)
    ser.write(b'{"cmd":"ping"}\n')
    time.sleep(0.2)
    resp = ser.readline().decode('utf-8', errors='replace').strip()
    if resp:
        print('RESPONSE:', resp)
    else:
        print('No response (timeout)')
    ser.close()
except Exception as e:
    print('Error opening/writing serial:', e)
