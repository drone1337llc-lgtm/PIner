cmake_minimum_required(VERSION 3.10)
project(PinerSolo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FIXED: Correct ARM flags for Pi Zero 2 W (ARMv8-A)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=armv8-a -mtune=cortex-a53")

set(SOURCES
    pi_miner_main.cpp
    mining_worker.cpp
    sha256_miner.cpp
    sha256_utils.cpp
    stratum_client.cpp
    web_server.cpp
)

set(HEADERS
    pi_miner_config.h
    mining_worker.h
    sha256_miner.h
    sha256_utils.h
    stratum_client.h
    web_server.h
    common.h
)

add_executable(pi_miner ${SOURCES} ${HEADERS})

# Link required libraries
target_link_libraries(pi_miner pthread ssl crypto)

# Include current directory for headers
target_include_directories(pi_miner PRIVATE ${CMAKE_SOURCE_DIR})
