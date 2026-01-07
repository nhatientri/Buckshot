cmake_minimum_required(VERSION 3.14)
project(BuckshotGame)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2 for Client
find_package(SDL2 REQUIRED)

# Common files
include_directories(src/common)
file(GLOB COMMON_SOURCES "src/common/*.cpp")

# Server
add_executable(server src/server/main.cpp src/server/Server.cpp src/server/UserManager.cpp src/server/GameSession.cpp ${COMMON_SOURCES})
if(UNIX)
    target_link_libraries(server PRIVATE pthread)
endif()

# Client
add_executable(client src/client/main.cpp src/client/Client.cpp src/client/GameUI.cpp ${COMMON_SOURCES})
target_include_directories(client PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(client PRIVATE ${SDL2_LIBRARIES})
if(UNIX)
    target_link_libraries(client PRIVATE pthread)
endif()
