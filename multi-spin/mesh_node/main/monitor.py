import socket
import struct

# Define the server address and port
SERVER_IP = '0.0.0.0'  # Listen on all available interfaces
SERVER_PORT = 5000

# Define the buffer size for receiving data
BUFFER_SIZE = 2048

# Function to unpack the received CSI data
def unpack_csi_data(data):
    # Assuming the first 128 bytes are the CSI data (signed char), followed by 6 bytes MAC address
    csi_values = struct.unpack('128b', data[:128])
    mac_address = data[128:134]
    mac_address = ':'.join(format(byte, '02x') for byte in mac_address)
    return csi_values, mac_address

# Create a UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((SERVER_IP, SERVER_PORT))

print(f"Server listening on {SERVER_IP}:{SERVER_PORT}")

while True:
    # Receive data from the ESP32 nodes
    data, addr = sock.recvfrom(BUFFER_SIZE)
    print(f"Received {len(data)} bytes from {addr}")
    
    # Unpack and display the data
    csi_values, mac_address = unpack_csi_data(data)
    print(f"Source MAC: {mac_address}")
    print("CSI Values:", csi_values[:10])  # Print the first 10 CSI values as an example

    # Process the CSI data as needed
    # ...
