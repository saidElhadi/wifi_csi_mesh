import serial
import time
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import numpy as np
from mpl_toolkits.mplot3d import Axes3D

# Prompt the user for the serial port name and baud rate
SERIAL_PORT = input("Enter the serial port (e.g., COM3 or /dev/ttyUSB0): ")
BAUD_RATE = int(input("Enter the baud rate (e.g., 115200): "))

# Open the serial port
ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)

# Prompt the user for the tag number to process
try:
    TAG_NUMBER = int(input("Enter the tag number to process: "))
except ValueError:
    print("Invalid tag number. Using default tag number 0.")
    TAG_NUMBER = 0

# File to save data
DATA_FILE = 'csi_data.csv'

# Initialize data lists
timestamps = []
csi_amp_values = []

# Parameters for plotting
EXPECTED_CSI_LENGTH = 64  # Adjust based on your data
max_frames = 100  # Number of time frames to display in the plot

# Create a figure and axis for plotting
fig = plt.figure()
ax = fig.add_subplot(111, projection='3d')

def clean_csi_data(csi_str, expected_length=None):
    # Remove any invalid characters and split the CSI data
    csi_str = csi_str.replace(' ', '')  # Remove spaces
    csi_list = csi_str.split(',')

    # Convert strings to integers
    amp_list = []
    for amp_str in csi_list:
        # Check if the string is a valid integer
        if amp_str.strip().replace('-', '').isdigit():
            amp = int(amp_str)
            amp_list.append(amp)
        else:
            # Invalid data detected
            return None

    # Normalize length if expected_length is provided
    if expected_length is not None:
        if len(amp_list) > expected_length:
            amp_list = amp_list[:expected_length]
        elif len(amp_list) < expected_length:
            amp_list.extend([0] * (expected_length - len(amp_list)))

    return amp_list

def parse_line(line, expected_length=None):
    # Split the line into data packets if multiple packets are concatenated
    packets = line.strip().split('\n')
    data_entries = []
    for packet in packets:
        # Ensure the packet contains exactly two colons
        if packet.count(':') != 2:
            continue
        try:
            tag_str, timestamp_str, csi_str = packet.strip().split(':')
            # Ensure that tag and timestamp are numeric
            if not tag_str.isdigit() or not timestamp_str.isdigit():
                continue
            tag = int(tag_str)
            timestamp = int(timestamp_str)
            amp_list = clean_csi_data(csi_str, expected_length)
            if amp_list is None:
                continue  # Skip this packet due to invalid data
            data_entries.append((tag, timestamp, amp_list))
        except Exception as e:
            print(f"Error parsing packet: {packet}")
            print(e)
            continue
    return data_entries

def animate(i):
    # Read data from the serial port
    raw_line = ser.readline()
    if not raw_line:
        return
    try:
        line = raw_line.decode('utf-8', errors='ignore')
        data_entries = parse_line(line, expected_length=EXPECTED_CSI_LENGTH)
        for tag, timestamp, amp_list in data_entries:
            # Process only the data with the specified tag number
            if tag != TAG_NUMBER:
                continue

            # Save data to lists
            timestamps.append(timestamp)
            csi_amp_values.append(amp_list)

            # Keep only the latest max_frames entries
            if len(timestamps) > max_frames:
                timestamps[:] = timestamps[-max_frames:]
                csi_amp_values[:] = csi_amp_values[-max_frames:]

            # Save data to file
            with open(DATA_FILE, 'a') as f:
                csi_str = ','.join(map(str, amp_list))
                f.write(f"{tag},{timestamp},{csi_str}\n")

            # Prepare data for plotting
            ax.clear()

            # Convert data to numpy arrays for plotting
            X = np.array(timestamps)
            Y = np.arange(EXPECTED_CSI_LENGTH)
            X, Y = np.meshgrid(X, Y)
            Z = np.array(csi_amp_values).T  # Transpose to match dimensions

            # Plot the surface
            ax.plot_surface(X, Y, Z, cmap='viridis')

            ax.set_title(f"CSI Amplitude Surface Plot for Tag {TAG_NUMBER}")
            ax.set_xlabel('Timestamp')
            ax.set_ylabel('Subcarrier Index')
            ax.set_zlabel('Amplitude')

            # Adjust the view angle for better visualization
            ax.view_init(elev=30, azim=-60)

            # Break after processing the first matching entry
            # because we read one line at a time
            break
    except Exception as e:
        print(f"Error processing line: {line}")
        print(e)

# Set up the animation
ani = animation.FuncAnimation(fig, animate, interval=100)

# Show the plot
plt.show()

# Close the serial port when done
ser.close()
