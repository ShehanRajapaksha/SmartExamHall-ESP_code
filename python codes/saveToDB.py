import serial
import time
import argparse
import struct
import mysql.connector
import adafruit_fingerprint


IMAGE_WIDTH = 256
IMAGE_HEIGHT = 288
IMAGE_DEPTH = 8

IMAGE_START_SIGNATURE = b'\xAA'

def getFingerprintImage(portNum, baudRate):
    conn = mysql.connector.connect(
        host="localhost",
        user="root",
        password="",
        database="test"
    )
    cursor = conn.cursor()

    try:
        port = serial.Serial(portNum, baudRate, timeout=0.1, inter_byte_timeout=0.1)
        print("Serial port opened successfully.")
    except Exception as e:
        print('Port open failed:', e)
        return False

    try:
        # Assume everything received at first is printable
        currByte = ''
        while currByte != IMAGE_START_SIGNATURE:
            currByte = port.read()
            print(currByte.decode(errors='ignore'), end='')

        # The datasheet says the sensor sends 1 byte for every 2 pixels
        totalBytesExpected = (IMAGE_WIDTH * IMAGE_HEIGHT) // 2

        image_data = bytearray()
        for i in range(totalBytesExpected):
            currByte = port.read()

            # Exit if we failed to read anything within the defined timeout
            if not currByte:
                print("Read timed out.")
                return False

            # Since each received byte contains info for 2 adjacent pixels,
            # assume that both pixels were originally close enough in colour
            # to now be assigned the same colour
            image_data.extend([currByte[0], currByte[0]])

        # Insert the image data into the database as a BLOB
        cursor.execute("INSERT INTO fingerprint (fingerprint_data) VALUES (%s)", (image_data,))
        conn.commit()

        print("Fingerprint image saved to database.")
        return True

    except KeyboardInterrupt:
        return False

    except Exception as e:
        print("getFingerprintImage failed: ", e)
        return False

    finally:
        port.close()
        cursor.close()
        conn.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Read and save a fingerprint image from the Arduino to a local MySQL database.")
    parser.add_argument("portNum", help="COM/Serial port (e.g. COM3 or /dev/ttyACM1)")
    parser.add_argument("baudRate", type=int, help="Baud rate (e.g. 57600)")

    args = parser.parse_args()
    getFingerprintImage(args.portNum, args.baudRate)
