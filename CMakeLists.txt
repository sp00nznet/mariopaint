cmake_minimum_required(VERSION 3.16)
project(MarioPaintRecomp C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# SDL2
find_package(SDL2 REQUIRED)

# --- snesrecomp: real SNES hardware backend (LakeSnes + adapter) ---
set(SNESRECOMP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(ext/snesrecomp)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# --- mp_game (static lib: recompiled game code) ---
add_library(mp_game STATIC
    src/recomp/mp_boot.c
    src/recomp/mp_bank01.c
    src/recomp/mp_input.c
    src/recomp/mp_gamelogic.c
    src/recomp/mp_gfxinit.c
    src/recomp/mp_sprites.c
    src/recomp/mp_canvas.c
    src/recomp/mp_audio.c
    src/recomp/mp_title.c
    src/recomp/mp_helpers.c
    src/recomp/mp_titleloop.c
    src/recomp/mp_interact.c
    src/recomp/mp_tools.c
)
target_link_libraries(mp_game PUBLIC snesrecomp)

# --- mp_launcher (executable) ---
add_executable(mp_launcher src/main/main.c)
target_link_libraries(mp_launcher PRIVATE
    mp_game
    snesrecomp
    SDL2::SDL2main
)
