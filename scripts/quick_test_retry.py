import serial
import sys
import time

if len(sys.argv) < 2:
    print('Usage: python quick_test_retry.py <PORT> [tries]')
    sys.exit(1)

port = sys.argv[1]
tries = int(sys.argv[2]) if len(sys.argv) > 2 else 6

for attempt in range(1, tries+1):
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
        sys.exit(0)
    except Exception as e:
        print(f'Attempt {attempt}/{tries}: cannot open {port}: {e}')
        if attempt < tries:
            time.sleep(1)
        else:
            print('All attempts failed. Close any program using the port (Serial Monitor, PuTTY, etc.) and retry.')
            sys.exit(2)
