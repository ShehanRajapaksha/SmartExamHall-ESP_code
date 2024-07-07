import serial
import time
import mysql.connector

# Define the serial port and baud rate for your sensor
port = 'COM6'  # Change to your port (e.g., '/dev/ttyUSB0' for Linux)
baud_rate = 57600

# Initialize the serial connection
ser = serial.Serial(port, baud_rate, timeout=1)

def send_command(command):
    ser.write(command)
    time.sleep(1)
    response = ser.read(ser.in_waiting)
    return response

def enroll_fingerprint(fingerprint_data, position):
    """
    Enroll fingerprint data to the sensor.
    :param fingerprint_data: The fingerprint data to enroll.
    :param position: The position ID to store the fingerprint data.
    """
    # Command to start the enrollment process
    start_enroll_command = b'\xEF\x01\xFF\xFF\xFF\xFF\x01\x00\x03\x01\x00\x05'
    response = send_command(start_enroll_command)
    print('Start Enroll Response:', response)

    # Command to store fingerprint at the given position
    store_finger_command = b'\xEF\x01\xFF\xFF\xFF\xFF\x01\x00\x06\x06' + position.to_bytes(2, 'big') + b'\x00\x00\x0E'
    response = send_command(store_finger_command)
    print('Store Finger Response:', response)

    # Send the fingerprint data
    ser.write(fingerprint_data)
    time.sleep(2)
    response = ser.read(ser.in_waiting)
    print('Send Fingerprint Data Response:', response)

# Function to fetch fingerprint data from the database
def fetch_fingerprints_from_db():
    conn = mysql.connector.connect(
        host="localhost",
        user="root",
        password="",
        database="test"
    )
    cursor = conn.cursor()

    cursor.execute("SELECT id, fingerprint_data FROM fingerprint")
    fingerprints = cursor.fetchall()

    cursor.close()
    conn.close()

    return fingerprints

# Main function to enroll fingerprints from the database
def main():
    fingerprints = fetch_fingerprints_from_db()

    for fingerprint in fingerprints:
        position, fingerprint_data = fingerprint
        print(f"Enrolling fingerprint at position {position}")
        enroll_fingerprint(fingerprint_data, position)

    ser.close()

if __name__ == "__main__":
    main()
