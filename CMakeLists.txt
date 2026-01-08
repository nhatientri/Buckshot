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
# Find SDL2_mixer using PkgConfig (more robust on Mac/Homebrew)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)
include_directories(${SDL2_MIXER_INCLUDE_DIRS})
link_directories(${SDL2_MIXER_LIBRARY_DIRS})

# Find SDL2_image using PkgConfig
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
include_directories(${SDL2_IMAGE_INCLUDE_DIRS})
link_directories(${SDL2_IMAGE_LIBRARY_DIRS})

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
add_executable(server src/server/main.cpp src/server/Server.cpp src/server/UserManager.cpp src/server/GameSession.cpp src/server/ReplayManager.cpp ${COMMON_SOURCES})
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
target_link_libraries(client_gui PRIVATE ${SDL2_LIBRARIES} ${SDL2_MIXER_LIBRARIES} ${SDL2_IMAGE_LIBRARIES})

# Platform Specific Linking
if (APPLE)
    target_link_libraries(client_gui PRIVATE "-framework OpenGL" "-framework CoreFoundation")
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(client_gui PRIVATE ${OPENGL_LIBRARIES})
endif()
