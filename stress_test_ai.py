import asyncio
import struct
import random
import time

HOST = '127.0.0.1'
PORT = 8080
CLIENT_COUNT = 1000 # High load AI test
DURATION = 15

# Constants
CMD_LOGIN = 2
CMD_GAME_STATE = 4
CMD_GAME_MOVE = 9
CMD_PLAY_AI = 6

SHOOT_SELF = 0
SHOOT_OPPONENT = 1

async def run_ai_client(client_id):
    try:
        reader, writer = await asyncio.open_connection(HOST, PORT)
        
        # 1. Login
        username = f"AiTester_{client_id}".ljust(32, '\0')[:32]
        password = "password".ljust(32, '\0')[:32]
        payload = username.encode('utf-8') + password.encode('utf-8')
        writer.write(struct.pack('II', len(payload), CMD_LOGIN) + payload)
        await writer.drain()
        
        # 2. Start AI Game
        writer.write(struct.pack('II', 0, CMD_PLAY_AI))
        await writer.drain()
        
        # 3. Game Loop
        start_time = time.time()
        while time.time() - start_time < DURATION:
            # Read Header
            header_data = await reader.readexactly(8)
            size, cmd = struct.unpack('II', header_data)
            
            # Read Body
            if size > 0:
                body = await reader.readexactly(size)
            
            if cmd == CMD_GAME_STATE:
                # Parse simplified state to see whose turn it is
                # Offset 106 is roughly where CurrentTurnUser string is (referencing Protocol.h/GameSession layout)
                # But parsing binary blobs without the struct layout in python is annoying.
                # Let's just blindly spam a move if we get a state packet, assuming the server ignores invalid moves.
                # It's a stress test, not a correctness test.
                
                # Try to shoot opponent
                move_payload = struct.pack('ii', SHOOT_OPPONENT, 0) # type, item
                writer.write(struct.pack('II', len(move_payload), CMD_GAME_MOVE) + move_payload)
                await writer.drain()
                
        writer.close()
        await writer.wait_closed()
        return True
    except Exception as e:
        # print(e)
        return False

async def main():
    print(f"Starting {CLIENT_COUNT} AI Simulation Clients...")
    tasks = []
    batch_size = 100
    for i in range(0, CLIENT_COUNT, batch_size):
        for j in range(batch_size):
            if i + j < CLIENT_COUNT:
                tasks.append(run_ai_client(i + j))
        await asyncio.sleep(0.1)
    
    results = await asyncio.gather(*tasks)
    success = results.count(True)
    print(f"AI Test Finished. Active Sessions: {success}/{CLIENT_COUNT}")

if __name__ == '__main__':
    import resource
    resource.setrlimit(resource.RLIMIT_NOFILE, (10000, 10000))
    asyncio.run(main())
