import serial.tools.list_ports as lp

ports = list(lp.comports())
if not ports:
    print('No serial ports found')
else:
    for p in ports:
        print(f'{p.device} - {p.description}')
