FROM ubuntu:22.04

# Avoid interactive prompts during apt installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and SDL2 libraries
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libsdl2-dev \
    libsdl2-mixer-dev \
    libsdl2-image-dev \
    libsqlite3-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy current directory context to /app in container
COPY . .

# Create build directory and run CMake
RUN mkdir -p build && cd build && \
    cmake .. && \
    make

# By default, run the server (headless)
CMD ["./build/server"]
