# SLAM Data Recorder HMI Firmware

This repository contains the firmware for the **CYD ESP32-2432S028** display module.  
The firmware provides a simple Human-Machine Interface (HMI) used to start and stop ROS2 bag recordings on a Raspberry Pi 5. These recordings are later used in SLAM workflows such as Cartographer, FAST_LIO, and KISS-ICP.

## Important Note — UART/USB Warning

**This firmware will only work if the USB connection on the CYD ESP32-2432S028 is NOT in use.**

The reason is:

- The **P1 connector TX/RX pins (GPIO 1 and 3)** share the same UART channel as  
  the **USB-to-Serial interface on the USB-C port**.
- If the USB-C port is connected, the UART is occupied and **the Pi will not receive commands**.
- **Unplug USB-C after flashing** the firmware to allow UART communication to function normally.

If USB remains connected, the device will power on normally but **serial communication to the Raspberry Pi 5 will not work**.

---

## Screenshots

Below are sample images of the HMI running on the CYD ESP32-2432S028:

![HMI Screenshot 1](https://github.com/user-attachments/assets/0b9cafda-170c-433f-b82e-69ca8a82b352)

![HMI Screenshot 2](https://github.com/user-attachments/assets/77c720c6-4911-4ea6-94b7-62b6a2d49a4b)

---

## Overview

The HMI communicates with the Raspberry Pi 5 using **UART serial** via:

- **P1 connector on the CYD ESP32-2432S028**  
- **GPIO 1 (TX)** and **GPIO 3 (RX)** for serial communication

Through this link, the ESP32 sends commands to the Pi to start or stop data collection, giving you a clean touchscreen-based interface for field recording without interacting directly with the Pi.

## Features
- Touchscreen control for starting and stopping ROS2 bag recordings  
- UART communication to the Raspberry Pi 5 using GPIO 1/3  
- Timestamped recording session organization  
- Minimal firmware designed specifically for SLAM dataset collection  
- Runs entirely on the CYD ESP32-2432S028

## Getting Started

### Hardware Required
- CYD ESP32-2432S028  
- Raspberry Pi 5  
- UART wiring between:  
  - ESP32 **GPIO 1 (TX)** → Pi **RX**  
  - ESP32 **GPIO 3 (RX)** → Pi **TX**  
  - Common Ground

### Flashing the Firmware
Clone the repository:

```bash
git clone https://github.com/tthom289/SLAM_Data_Recorder_HMI.git
cd SLAM_Data_Recorder_HMI
Build and flash with PlatformIO or ESP-IDF:

bash
Copy code
pio run --target upload
or

bash
Copy code
idf.py build
idf.py flash
After flashing, unplug USB-C before attempting UART communication.

Project Structure
makefile
Copy code
SLAM_Data_Recorder_HMI/
│── src/                   # ESP32 firmware source code
│── include/               # Configuration and headers
│── platformio.ini or idf_project_files
│── README.md
Usage
Flash the firmware

Unplug USB-C from the CYD ESP32-2432S028

Power the ESP32 and Raspberry Pi 5 normally

The HMI initializes and displays recording controls

Press Start to command the Pi over UART to begin a ROS2 bag

Press Stop to finalize the recording

Retrieve the bag files from the Pi for use in SLAM tools

License
This project is licensed under the MIT License unless otherwise stated.
