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

target_include_directories(app PRIVATE interface interface/generated include src/motors)
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

# -------- Interface descriptor codegen --------
# Regenerates interface/generated/loki_{gatt,coap}.h from the YAML sources at
# every full build. If Python 3.8+ is missing the regeneration is skipped with
# a warning; the checked-in generated headers are then used as-is. The script
# itself also skips (warn + exit 0) if PyYAML is unavailable.
find_package(Python3 3.8 COMPONENTS Interpreter QUIET)

set(LOKI_GEN_SCRIPT   ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_descriptors.py)
set(LOKI_GEN_GATT     ${CMAKE_CURRENT_SOURCE_DIR}/interface/gatt.yaml)
set(LOKI_GEN_COAP     ${CMAKE_CURRENT_SOURCE_DIR}/interface/coap.yaml)
set(LOKI_GEN_OUT_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/interface/generated)
set(LOKI_GEN_OUTPUTS
    ${LOKI_GEN_OUT_DIR}/loki_gatt.h
    ${LOKI_GEN_OUT_DIR}/loki_coap.h
)

if(Python3_Interpreter_FOUND)
    add_custom_command(
        OUTPUT  ${LOKI_GEN_OUTPUTS}
        COMMAND ${Python3_EXECUTABLE} ${LOKI_GEN_SCRIPT}
                --gatt    ${LOKI_GEN_GATT}
                --coap    ${LOKI_GEN_COAP}
                --out-dir ${LOKI_GEN_OUT_DIR}
        DEPENDS ${LOKI_GEN_SCRIPT} ${LOKI_GEN_GATT} ${LOKI_GEN_COAP}
        COMMENT "Regenerating Loki interface descriptors from YAML"
        VERBATIM
    )
    add_custom_target(loki_gen_descriptors ALL DEPENDS ${LOKI_GEN_OUTPUTS})
    add_dependencies(app loki_gen_descriptors)
else()
    message(WARNING
        "Python 3.8+ not found. Loki interface descriptor regeneration is "
        "disabled; the checked-in headers under interface/generated/ will be "
        "used as-is. Install Python 3.8+ and PyYAML to enable codegen.")
endif()