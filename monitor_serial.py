import serial
import time
import sys

# Configuration
PORT = 'COM90'
BAUDRATE = 115200
TIMEOUT = 0.1

print(f"Connecting to {PORT} at {BAUDRATE} baud...")

try:
    ser = serial.Serial(PORT, BAUDRATE, timeout=TIMEOUT)
    print(f"Connected! Monitoring serial output (Press Ctrl+C to stop)...\n")
    print("=" * 70)
    
    while True:
        if ser.in_waiting > 0:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').rstrip()
                if line:
                    print(line)
            except Exception as e:
                print(f"Error reading line: {e}")
        time.sleep(0.01)
        
except KeyboardInterrupt:
    print("\n\nStopped by user")
except serial.SerialException as e:
    print(f"Error opening serial port: {e}")
except Exception as e:
    print(f"Unexpected error: {e}")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
        print("Serial port closed")
