cmake_minimum_required(VERSION 3.14)
project(BuckshotGame)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2 for Client
# Common files
include_directories(src/common)
file(GLOB COMMON_SOURCES "src/common/*.cpp")

# Find SDL2
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

# ImGui Sources
set(IMGUI_DIR "libs/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends)

# Server Target
add_executable(server src/server/main.cpp src/server/Server.cpp src/server/UserManager.cpp src/server/GameSession.cpp ${COMMON_SOURCES})
target_link_libraries(server PRIVATE pthread)

# Client Console Target
add_executable(client src/client/main.cpp src/client/Client.cpp ${COMMON_SOURCES})
target_link_libraries(client PRIVATE pthread)

# Client GUI Target
add_executable(client_gui
    src/client/GuiMain.cpp
    src/client/NetworkClient.cpp
    src/client/GameUI.cpp
    ${COMMON_SOURCES}
    ${IMGUI_SOURCES}
)
target_link_libraries(client_gui PRIVATE ${SDL2_LIBRARIES} "-framework OpenGL" "-framework CoreFoundation")
