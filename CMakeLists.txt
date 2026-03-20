cmake_minimum_required(VERSION 3.20)
project(jam_protocol C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lm")

# ── Main project ──────────────────────────────────────
add_executable(jam_protocol
        main.c
        jammer.c
        monitor.c
        adaptive.c
        simulation.c
)
target_compile_options(jam_protocol PRIVATE -Wall -Wextra)

# ── Test targets ──────────────────────────────────────
add_executable(test_jammer
        test_jammer.c
        jammer.c
)
target_compile_options(test_jammer PRIVATE -Wall -Wextra)

add_executable(test_monitor
        test_monitor.c
        monitor.c
)
target_compile_options(test_monitor PRIVATE -Wall -Wextra)

add_executable(test_adaptive
        test_adaptive.c
        adaptive.c
        monitor.c
        jammer.c
)
target_compile_options(test_adaptive PRIVATE -Wall -Wextra)