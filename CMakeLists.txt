#
# Copyright (c) 2020 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(coap_loki)

FILE(GLOB app_sources src/*.c src/displays/*.c src/motors/.c)
FILE(GLOB motor_sources drivers/motor/*.c)



# NORDIC SDK APP START
target_sources(app PRIVATE ${app_sources})

target_include_directories(app PRIVATE interface include src/motors)
# NORDIC SDK APP END

target_sources(app PRIVATE ${motor_sources})

target_include_directories(app PRIVATE 
    drivers/motor
)

# -------- Firmware version (MANUAL) --------
set(APP_VERSION_MAJOR 1)
set(APP_VERSION_MINOR 4)
set(APP_VERSION_PATCH 2)

# -------- Build number (AUTO) --------
string(TIMESTAMP APP_BUILD_NUMBER "%Y%m%d%H%M%S" UTC)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/app_version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/app_version.h
  @ONLY
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})