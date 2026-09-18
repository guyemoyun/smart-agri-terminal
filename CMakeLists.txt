cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Cube_MX_Code)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add user sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Application layer
    ${CMAKE_CURRENT_SOURCE_DIR}/User/app/app_main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/app/PWM.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/app/LED.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/app/SW.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/app/beep.c

    # BSP / device drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/User/bsp/debug.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/bsp/CO2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/bsp/dht22.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/bsp/ISR_callback.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/bsp/light.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/bsp/soil.c

    # Sensor service
    ${CMAKE_CURRENT_SOURCE_DIR}/User/service/sensor_service.c
    # OLED
    ${CMAKE_CURRENT_SOURCE_DIR}/User/oled/oled.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/oled/oled_demo.c
)

# DHT22 uses microsecond-level pulse timing. Keep this source optimized even in Debug builds.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/User/bsp/dht22.c
    PROPERTIES COMPILE_OPTIONS "-O2"
)
# Add user include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/User/app
    ${CMAKE_CURRENT_SOURCE_DIR}/User/bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/User/oled
    ${CMAKE_CURRENT_SOURCE_DIR}/User/service
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
