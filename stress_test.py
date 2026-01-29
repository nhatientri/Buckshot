import socket
import threading
import time
import struct
import random

# Configuration
HOST = 'localhost'
PORT = 8080
CLIENT_COUNT = 100  # Number of clients to simulate
DURATION = 10       # Seconds to run

# Protocol Constants
CMD_LOGIN = 2
CMD_OK = 3
CMD_QUEUE_JOIN = 10

def client_task(client_id):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((HOST, PORT))
        
        # Login
        username = f"Bot_{client_id}"
        password = "password"
        payload = struct.pack('32s32s', username.encode('utf-8'), password.encode('utf-8'))
        header = struct.pack('II', len(payload), CMD_LOGIN)
        s.sendall(header + payload)
        
        # Join Queue
        header = struct.pack('II', 0, CMD_QUEUE_JOIN)
        s.sendall(header)
        
        # Keep connection open
        start = time.time()
        while time.time() - start < DURATION:
            time.sleep(1)
            # Maybe send a ping or something?
            # For now just keep open
            
        s.close()
    except Exception as e:
        print(f"Client {client_id} failed: {e}")

threads = []
print(f"Starting {CLIENT_COUNT} clients...")

for i in range(CLIENT_COUNT):
    t = threading.Thread(target=client_task, args=(i,))
    threads.append(t)
    t.start()
    time.sleep(0.01) # Stagger slightly

for t in threads:
    t.join()

print("Test Completed.")
