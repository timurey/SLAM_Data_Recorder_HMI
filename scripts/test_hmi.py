#!/usr/bin/env python3
"""Send a test status packet to the CYD HMI to simulate a healthy system."""

import serial
import json
import time
import sys

PORT = '/dev/cu.usbserial-0001'
BAUD = 115200
COUNT = 0        # 0 = run forever, Ctrl-C to stop
INTERVAL = 0.5

msg = {
    'sensors_running': True,
    'lidar_hz': 10.2,
    'imu_hz': 200.0,
    'lidar_ok': True,
    'imu_ok': True,
    'recording': False,
    'disk_gb': 52.3,
    'rec_duration': 0,
    'bag_name': '',
    'wifi_ip': '192.168.1.42',
    'wifi_mode': 'ap',
}

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    with serial.Serial(port, BAUD, timeout=1) as ser:
        time.sleep(0.5)
        i = 0
        while COUNT == 0 or i < COUNT:
            i += 1
            msg['rec_duration'] = i // 2  # simulate a running recording timer
            ser.write((json.dumps(msg) + '\n').encode())
            print(f'[{i}] sent')
            time.sleep(INTERVAL)
