# Buckshot Roulette Online

A multiplayer implementation of the popular "Buckshot Roulette" game, written in C++ using TCP sockets, SDL2, and Dear ImGui.

## Overview

This project implements a client-server architecture for 1v1 online matches. It features a robust server handling game logic, user authentication, and matchmaking, along with a graphical client for gameplay.

## Features

- **Multiplayer (1v1)**: Real-time turn-based gameplay over TCP.
- **AI Opponent**: "Practice vs AI" mode ("The Dealer") for offline-style play.
- **Lobby System**:
    - User Authentication (Register/Login).
    - Real-time Online Player List.
    - Challenge System (Send/Accept/Decline).
    - Rematch functionality.
- **Game Mechanics**:
    - Full implementation of Buckshot Roulette rules.
    - Items: Beer, Cigarettes, Handcuffs, Magnifying Glass, Saw, Inverter, Medicine, Adrenaline (Note: Some valid items implemented).
    - HP tracking, Ammo loading (Live vs Blank).
    - **Item Usage Limit**: Max 2 items per turn for balance.
- **Replay System**:
    - Server records all matches.
    - Client can browse and watch replays of past games.
- **Elo Rating System**: Skill-based matchmaking and ranking.
- **Anti-Cheat**: Server-authoritative logic.

## Prerequisites

- **C++ Compiler**: C++17 or later.
- **CMake**: 3.10 or later.
- **SDL2**: Graphics and input library.
- **OpenGL**: 3.0+ / Core Profile (3.2+ on macOS).
- **Dear ImGui**: Included or linked (Standard backend used).

### macOS (Brew)
```bash
brew install sdl2 cmake
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

This will produce three executables:
- `server`: The game server.
- `client`: A CLI-based client (legacy/debug).
- `client_gui`: The main graphical client.

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

### 3. Gameplay
1. **Login/Register**: Enter a username and password.
2. **Lobby**:
   - See online users on the right.
   - Click **"Challenge"** next to a user to invite them.
   - Click **"Practice vs AI"** to play against the bot.
   - Use **"Watch Replays"** to review past matches.
3. **In-Game**:
   - Click items to use them (Max 2 per turn).
   - Click "Shoot Self" or "Shoot Opponent" to end your move (if using a gun).

## Project Structure

- `src/common`: Shared definitions (`Protocol.h`).
- `src/server`: Server-side logic (`Server.cpp`, `GameSession.cpp`, `UserManager.cpp`, `ReplayManager.cpp`).
- `src/client`: Client-side logic (`GuiMain.cpp`, `NetworkClient.cpp`, `Client.cpp`).
- `include`: Header files (if separated).
- `replays`: Directory where server stores `.replay` files.
- `users.txt`: Flat-file database for user credentials and stats.

## Protocol

Communication uses a custom binary protocol with a `PacketHeader` (Command + Size) followed by a payload structure (e.g., `LoginRequest`, `GameStatePacket`). See `src/common/Protocol.h` for details.

## Context for Handoff

- **Compilation**: The project compiles successfully on macOS.
- **Known Issues**: None currently. Item limit logic and AI integration were recently fixed.
- **Recent Changes**:
    - Added "Practice vs AI" button to Lobby.
    - Fixed compilation errors in `Server.cpp` due to missing `CMD_PLAY_AI` handler.
    - Implemented 2-item usage limit per turn.

---
*Created by Antigravity Agent*
