# Copyright (c) 2020 ARM Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.19.0 FATAL_ERROR)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(MBED_PATH ${REPO_ROOT}/mbed/mbed-os CACHE INTERNAL "")
set(ASP3_PATH ${REPO_ROOT}/asp3 CACHE INTERNAL "")
set(RTOS_BRIDGE_PATH ${REPO_ROOT}/rtos-bridge)
set(MBED_CONFIG_PATH ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(APP_TARGET blinky)

include(${MBED_PATH}/tools/cmake/app.cmake)

project(${APP_TARGET})

add_subdirectory(${MBED_PATH} ${CMAKE_CURRENT_BINARY_DIR}/mbed-os)

add_executable(${APP_TARGET})

mbed_configure_app_target(${APP_TARGET})

target_include_directories(${APP_TARGET}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${MBED_PATH}/cmsis/CMSIS_5/CMSIS/RTOS2/Include
        ${MBED_PATH}/cmsis/CMSIS_5/CMSIS/RTOS2/RTX/Config
        ${MBED_PATH}/cmsis/CMSIS_5/CMSIS/RTOS2/RTX/Include
        ${MBED_PATH}/cmsis/CMSIS_5/CMSIS/RTOS2/RTX/Include1
        ${MBED_PATH}/cmsis/CMSIS_5/CMSIS/RTOS2/RTX/Source
        ${MBED_PATH}/cmsis/device/rtos/include
        ${ASP3_PATH}
        ${ASP3_PATH}/arch/gcc
        ${ASP3_PATH}/arch/arm_m_gcc/mbed
        ${ASP3_PATH}/arch/arm_m_gcc/common
        ${ASP3_PATH}/include
        ${ASP3_PATH}/kernel
        ${ASP3_PATH}/syssvc
        ${RTOS_BRIDGE_PATH}
        asp
)

target_sources(${APP_TARGET}
    PRIVATE
        ${MBED_PATH}/cmsis/device/rtos/TOOLCHAIN_GCC_ARM/mbed_boot_gcc_arm.c
        ${MBED_PATH}/cmsis/device/rtos/source/mbed_boot.c
        ${MBED_PATH}/cmsis/device/rtos/source/mbed_rtos_rtx.c
        main.cpp
        ${REPO_ROOT}/target_kernel_impl.cpp
        ${REPO_ROOT}/target_serial.cpp
        ${REPO_ROOT}/target_timer.cpp
        ${RTOS_BRIDGE_PATH}/rtos-bridge.c
)

target_link_directories(${APP_TARGET}
    PRIVATE
        asp
)

target_link_libraries(${APP_TARGET}
    PRIVATE
        mbed-baremetal
        asp
)

target_compile_definitions(${APP_TARGET}
    PRIVATE
        MBED_CONF_RTOS_PRESENT=1
        TOPPERS_OMIT_TECS
)

mbed_set_post_build(${APP_TARGET})

option(VERBOSE_BUILD "Have a verbose build process")
if(VERBOSE_BUILD)
    set(CMAKE_VERBOSE_MAKEFILE ON)
endif()
