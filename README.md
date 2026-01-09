# Buckshot Roulette Online

A multiplayer implementation of the popular "Buckshot Roulette" game, written in C++ using TCP sockets, SDL2, and Dear ImGui.

## Overview

This project implements a client-server architecture for 1v1 online matches. It features a robust server handling game logic, user authentication, and matchmaking, along with a graphical client for gameplay.

## Features

- **Multiplayer (1v1)**: Real-time turn-based gameplay over TCP.
- **Matchmaking Queue**: "Find Match" button to automatically pair with other waiting players.
- **AI Opponent**: "Practice vs AI" mode ("The Dealer") for offline-style play.
- **Pause Game**: Ability to pause and resume games against the AI.
- **Lobby System**:
    - User Authentication (Register/Login).
    - Real-time Online Player List.
    - Challenge System (Send/Accept/Decline).
    - Rematch functionality.
- **Game Mechanics**:
    - Full implementation of Buckshot Roulette rules.
    - Items: Beer, Cigarettes, Handcuffs, Magnifying Glass, Knife, Inverter, Medicine.
    - HP tracking, Ammo loading (Live vs Blank).
    - **Item Usage Limit**: Max 2 items per turn for balance.
- **Replay System**:
    - Server records all matches.
    - Client can browse and watch replays of past games.
- **Elo Rating System**: Skill-based matchmaking and ranking.
- **Cross-Platform**: Runs on macOS and Linux.

## Prerequisites

- **C++ Compiler**: C++17 or later (Clang/GCC).
- **CMake**: 3.10 or later.
- **SDL2**: Core library, Image, and Mixer extensions.
- **OpenGL**: 3.0+ / Core Profile.

### macOS (Homebrew)
```bash
brew install sdl2 sdl2_image sdl2_mixer cmake
```

### Linux (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev
```

## Build Instructions

1. Create a build directory:
   ```bash
   mkdir build
   cd build
   ```
2. Generate Makefiles:
   ```bash
   cmake ..
   ```
3. Compile:
   ```bash
   make
   ```

## Running the Game

### 1. Start the Server
```bash
./build/server
# OR specify a port (default 8080)
./build/server 9000
```
The server will start on port `8080` (or your custom port).

### 2. Start the Client
**Localhost (Single Computer):**
```bash
./build/client_gui
```

**LAN / Multi-Computer:**
Pass the Server's IP address (and optional Port) as arguments:
```bash
./build/client_gui 192.168.1.5
# OR with Port
./build/client_gui 192.168.1.5 9000
```
*(Replace `192.168.1.5` with the actual IP address of the server computer)*

## Docker Support

You can run the server in a Docker container (Linux environment).

1. **Build the Image**:
   ```bash
   docker build -t buckshot-server .
   ```

2. **Run the Server**:
   ```bash
   docker run -p 8080:8080 -it buckshot-server
   ```

> **Note**: The GUI Client should be run natively on your host machine to connect to the Dockerized server (`127.0.0.1:8080`).

## Running the Game

### 1. Start the Server
Run the server first. It listens on port 8080 by default.
```bash
./build/server
```

### 2. Start the Client
Run one or more clients.
```bash
./build/client_gui
```

### 3. Gameplay Guide
1. **Login/Register**: Enter a unique username and password.
2. **Lobby**:
   - **Find Match**: Join the queue to play against a random opponent.
   - **Practice vs AI**: Play a solo game against "The Dealer".
   - **Challenge**: Click "Challenge" next to a user in the Online list to invite them directly.
   - **Watch Replays**: Review past matches.
3. **In-Game**:
   - **Items**: Click items to use them (Max 2 per turn). hover to see descriptions.
   - **Shoot**: Click "SHOOT SELF" (risk for extra turn) or "SHOOT OPPONENT".
   - **Controls**:
     - **Pause**: (AI Only) Click "PAUSE" to freeze the game.
     - **Surrender**: Forfeit the match.

## Project Structure

- `src/common`: Shared definitions (`Protocol.h`).
- `src/server`: Server-side logic (`Server.cpp`, `GameSession.cpp`, `UserManager.cpp`, `ReplayManager.cpp`).
- `src/client`: Client-side logic (`GuiMain.cpp`, `NetworkClient.cpp`).
- `assets`: Game textures and sounds.
- `replays`: Directory where server stores `.replay` files.

## Protocol

Communication uses a custom binary protocol defined in `src/common/Protocol.h`. Packets consist of a `PacketHeader` (Command + Size) followed by a specific payload structure (e.g., `GameStatePacket`).

---