import serial
import ntplib
import csv
import os
import argparse
from time import ctime, time
from dataclasses import dataclass

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
        return response.tx_time
    except ntplib.NTPException as e:
        print(f"Error: {e}")
        return None

def get_device_time():
    try:
        ser = serial.Serial(port, baud, timeout=1)
        command = "GT\r\n"

        ser.write(command.encode())

        response = ser.readline().decode().strip()
        return int(response)

    except serial.SerialException as e:
        print(f"Error: {e}")
        return None

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
        print(f"Error: {e}")
        return False

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

    if args.com is not None:
        port = args.com
    else:
        port = "/dev/cu.usbmodem49795F7330551"
    
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