# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

target_sources(app PRIVATE src/main.c)
target_link_options(app PUBLIC -Wl,--just-symbols=${ZEPHYR_BASE}/../modules/hal/ingchips/bundles/noos_mini/ING9168xx/symdefs.g)