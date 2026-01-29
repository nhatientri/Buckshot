import asyncio
import struct
import random
import time

HOST = '127.0.0.1'
PORT = 8080
CLIENT_COUNT = 2000
DURATION = 5

# CMD Constants
CMD_LOGIN = 2
CMD_QUEUE_JOIN = 10

async def run_client(client_id):
    try:
        reader, writer = await asyncio.open_connection(HOST, PORT)
        
        # Login
        username = f"Bot_{client_id}".ljust(32, '\0')[:32]
        password = "password".ljust(32, '\0')[:32]
        payload = username.encode('utf-8') + password.encode('utf-8')
        header = struct.pack('II', len(payload), CMD_LOGIN)
        
        writer.write(header + payload)
        await writer.drain()
        
        # Read response? (Optional, skipping for speed/stress)
        
        # Join Queue
        header = struct.pack('II', 0, CMD_QUEUE_JOIN)
        writer.write(header)
        await writer.drain()

        # Keep alive
        await asyncio.sleep(DURATION)
        
        writer.close()
        await writer.wait_closed()
        return True
    except Exception as e:
        # print(f"Client {client_id} error: {e}")
        return False

async def main():
    print(f"Starting {CLIENT_COUNT} clients...")
    tasks = []
    # Create batches to avoid opening 2000 files instantly and hitting transient OS limits
    batch_size = 200
    for i in range(0, CLIENT_COUNT, batch_size):
        for j in range(batch_size):
            if i + j < CLIENT_COUNT:
                tasks.append(run_client(i + j))
        await asyncio.sleep(0.1) 
        print(f"Launched {min(i+batch_size, CLIENT_COUNT)}...")

    results = await asyncio.gather(*tasks)
    success = results.count(True)
    print(f"Finished. Success: {success}/{CLIENT_COUNT}")

if __name__ == '__main__':
    # Increase Limit if needed (though ulimit -n showed high enough)
    import resource
    resource.setrlimit(resource.RLIMIT_NOFILE, (10000, 10000))
    asyncio.run(main())
