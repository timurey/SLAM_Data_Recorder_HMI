#!/usr/bin/env python3
"""Send a test status packet to the CYD HMI to simulate a healthy system."""

import serial
import json
import time
import sys

PORT = '/dev/cu.usbserial-210'
BAUD = 115200
COUNT = 10
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
}

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    with serial.Serial(port, BAUD, timeout=1) as ser:
        time.sleep(0.5)
        for i in range(COUNT):
            ser.write((json.dumps(msg) + '\n').encode())
            print(f'[{i+1}/{COUNT}] sent: {json.dumps(msg)}')
            time.sleep(INTERVAL)
