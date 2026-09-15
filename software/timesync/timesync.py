import serial
import ntplib
import csv
import os
import argparse
import datetime

from zoneinfo import ZoneInfo
from time import ctime, time
from dataclasses import dataclass
from serial.tools import list_ports

tz = ZoneInfo("America/Chicago")

@dataclass
class TimeSyncRun:
    ntp_time: int
    device_time: int
    run_time: int
    skew: int

def get_ntp_time(ntp_server="pool.ntp.org"):
    try:
        client = ntplib.NTPClient()
        response = client.request(ntp_server)
        
        return __apply_dst(response.tx_time)
    except ntplib.NTPException as e:
        print(f"Error fetching time from NTP server: {e}")
        exit(-1)

def __apply_dst(time: int):
    return time + __get_dst_offset()

def __get_dst_offset():
    now = datetime.datetime.now(tz)
    if now.dst() != datetime.timedelta(0):
        return 3600
    else:
        return 0

# USB descriptor of the STM32 Shift Clock's CDC interface. The ST-Link shares the
# vendor id but reports a different product id, so the two never collide.
STM32_CDC_VID = 0x0483
STM32_CDC_PID = 0x5740

def find_device_port():
    matches = [p for p in list_ports.comports()
               if p.vid == STM32_CDC_VID and p.pid == STM32_CDC_PID]

    if not matches:
        print(f"Error: no STM32 CDC device (USB {STM32_CDC_VID:04x}:{STM32_CDC_PID:04x}) found.")
        print("Check that the board is plugged in, or name a port with --com <port>.")
        exit(-1)

    if len(matches) > 1:
        print("Error: more than one STM32 CDC device found. Choose one with --com <port>:")
        for p in matches:
            print(f"  {p.device}  serial={p.serial_number}")
        exit(-1)

    return matches[0].device

def get_device_time():
    try:
        ser = serial.Serial(port, baud, timeout=1)
        command = "GT\r\n"

        ser.write(command.encode())

        response = ser.readline().decode().strip()
        return int(response)

    except serial.SerialException as e:
        print(f"Error getting time from device: {e}")
        exit(-1)

    finally:
        if ser.is_open:
            ser.close()

def set_device_time(time: int):
    try:
        ser = serial.Serial(port, baud, timeout=1)
        command = f"ST {time}\r\n"

        ser.write(command.encode())

        response = ser.readline().decode().strip()
        if response == "OK":
            return True
        else:
            print(f"Error: {response}")
            return False

    except serial.SerialException as e:
        print(f"Error setting device time: {e}")
        exit(-1)

    finally:
        if ser.is_open:
            ser.close()

def calculate_skew(time1: int, time2: int):
    return time1 - time2
        

def write_csv_header(file_path: str):
    try:
        with open(file_path, mode="w") as file:
            writer = csv.writer(file)
            writer.writerow(["Run Time", "NTP Time", "Device Time", "Skew"])
    except Exception as e:
        print(f"Error writing to csv file: {e}")
        exit(-1)

def write_csv_row(file_path: str, tsr: TimeSyncRun):
    if not os.path.exists(file_path):
        write_csv_header(file_path)
    try:
        with open(file_path, mode="a") as file:
            writer = csv.writer(file)
            writer.writerow([tsr.run_time, tsr.ntp_time, tsr.device_time, tsr.skew])
    except Exception as e:
        print(f"Error writing to csv file: {e}")

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("-H", "--human", action="store_true", help="Human readable output")
    parser.add_argument("-U", "--update", action="store_true", help="Update device time to NTP time")
    parser.add_argument("--csv", action="store_true", help="Write output to CSV file")
    parser.add_argument("--com", type=str, help="COM port for device")
    args = parser.parse_args()

    port = args.com if args.com is not None else find_device_port()
    
    baud = 115200

    run_time = int(time())
    ntp_time = get_ntp_time()
    ntp_time = int(ntp_time)
    device_time = get_device_time()

    skew = calculate_skew(ntp_time, device_time)

    if args.csv:
        tsr = TimeSyncRun(ntp_time, device_time, run_time, skew)
        write_csv_row("timesync.csv", tsr)

    if abs(skew) > 1 & args.update:
        print("Time skew detected, setting device time to NTP time")
        set_device_time(ntp_time)
        device_time = get_device_time()
        skew = calculate_skew(ntp_time, device_time)

    if args.human:
        print(f"NTP Time: {ctime(ntp_time)}")
        print(f"Device Time: {ctime(device_time)}")
        print(f"Skew: {skew}")
    else:
        print(f"{run_time},{ntp_time},{device_time},{skew}")