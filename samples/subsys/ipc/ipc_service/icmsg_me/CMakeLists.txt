#
# Copyright (c) 2022 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(NOT ("${BOARD}" STREQUAL "nrf5340dk_nrf5340_cpuapp"))
  message(FATAL_ERROR "${BOARD} is not supported for this sample")
endif()

project(ipc_service)

target_sources(app PRIVATE src/main.c)

include(ExternalProject)
