# Copyright (c) 2020 ARM Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.19.0 FATAL_ERROR)

set(MBED_PATH ${CMAKE_CURRENT_SOURCE_DIR}/mbed-os CACHE INTERNAL "")
set(MBED_CONFIG_PATH ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(APP_TARGET mbed-asp3)

include(${MBED_PATH}/tools/cmake/app.cmake)

project(${APP_TARGET})

add_subdirectory(${MBED_PATH})

add_executable(${APP_TARGET})

mbed_configure_app_target(${APP_TARGET})

target_include_directories(${APP_TARGET}
    PRIVATE
        asp3
        asp3/arch/gcc
        asp3/arch/arm_m_gcc/mbed
        asp3/arch/arm_m_gcc/common
        asp3/include
        asp3/kernel
        asp3/obj
        asp3/syssvc
)

target_sources(${APP_TARGET}
    PRIVATE
        main.cpp
        target_kernel_impl.cpp
        target_serial.cpp
        target_timer.cpp
        asp3/sample/sample1.cpp
)

target_link_directories(${APP_TARGET}
    PRIVATE
        asp3/obj
)

target_link_libraries(${APP_TARGET}
    PRIVATE
        mbed-baremetal
        asp
)

target_compile_options(${APP_TARGET}
    PRIVATE
        -DTOPPERS_OMIT_TECS
)

mbed_set_post_build(${APP_TARGET})

option(VERBOSE_BUILD "Have a verbose build process")
if(VERBOSE_BUILD)
    set(CMAKE_VERBOSE_MAKEFILE ON)
endif()
