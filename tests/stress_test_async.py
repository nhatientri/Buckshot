import asyncio
import struct
import random
import time

HOST = '192.168.1.142'
PORT = 8080
CLIENT_COUNT = 2000
DURATION = 5

# CMD Constants
CMD_OK = 0
CMD_REGISTER = 1
CMD_LOGIN = 2
CMD_LOGIN_SUCCESS = 3
CMD_FAIL = 255
CMD_QUEUE_JOIN = 60
CMD_LIST_USERS_RESP = 6

CMD_NAMES = {
    0: "CMD_OK",
    1: "CMD_REGISTER",
    2: "CMD_LOGIN",
    3: "CMD_LOGIN_SUCCESS",
    255: "CMD_FAIL",
    60: "CMD_QUEUE_JOIN",
    6: "CMD_LIST_USERS_RESP"
}

def get_cmd_name(cmd):
    return CMD_NAMES.get(cmd, f"UNKNOWN({cmd})")

async def run_client(client_id):
    try:
        reader, writer = await asyncio.open_connection(HOST, PORT)
        
        # Login
        username = f"Bot_{client_id}".ljust(32, '\0')[:32]
        password = "password".ljust(32, '\0')[:32]
        payload = username.encode('utf-8') + password.encode('utf-8')
        # Register (Blindly attempt)
        header_reg = struct.pack('II', len(payload), 1) # CMD_REGISTER = 1
        writer.write(header_reg + payload)
        await writer.drain()

        # Read Register Response (8 bytes header)
        resp_header_data = await reader.read(8)
        if len(resp_header_data) == 8:
            resp_size, resp_cmd = struct.unpack('II', resp_header_data)
            resp_cmd &= 0xFF # Mask to get only the command byte
            # cmd 0 (OK) or 255 (FAIL - if already exists). Both are fine to proceed to login.
            if client_id < 5:
                print(f"[Client {client_id}] Register Response: {get_cmd_name(resp_cmd)}")

        # Login
        header = struct.pack('II', len(payload), CMD_LOGIN)
        writer.write(header + payload)
        await writer.drain()
        
        # Read Login Response
        resp_header_data = await reader.read(8)
        if len(resp_header_data) == 8:
            resp_size, resp_cmd = struct.unpack('II', resp_header_data)
            resp_cmd &= 0xFF # Mask to get only the command byte
            
            # If CMD_LOGIN_SUCCESS (3), perform extra read for stats
            if resp_cmd == CMD_LOGIN_SUCCESS: # CMD_LOGIN_SUCCESS
                 stats_data = await reader.read(resp_size) # Should be 12 bytes
                 if client_id < 5:
                      print(f"[Client {client_id}] Login Success! {get_cmd_name(resp_cmd)}, DataSize={resp_size}")
            elif client_id < 5:
                 print(f"[Client {client_id}] Login Response: {get_cmd_name(resp_cmd)}")
        
        # Join Queue
        header = struct.pack('II', 0, CMD_QUEUE_JOIN)
        writer.write(header)
        await writer.drain()

        # Read Queue Response
        resp_header_data = await reader.read(8)
        if len(resp_header_data) == 8 and client_id < 5:
             resp_size, resp_cmd = struct.unpack('II', resp_header_data)
             resp_cmd &= 0xFF
             print(f"[Client {client_id}] JoinQueue Response: {get_cmd_name(resp_cmd)}")

        # Keep alive
        await asyncio.sleep(DURATION)
        
        writer.close()
        await writer.wait_closed()
        return True
    except Exception as e:
        if client_id < 5:
            print(f"Client {client_id} error: {e}")
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
