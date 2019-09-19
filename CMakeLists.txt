#
# PCGR-Reader (c) 2019 Kavawuvi
#
# This program is free software under the GNU General Public License v3.0. See LICENSE for more information.
#

# Require at least version 3.10
cmake_minimum_required(VERSION 3.10)

# Project name - You should change this if you're making a fork
project(carnage-reporter
    VERSION 0.1.0
    DESCRIPTION "Postgame Carnage Report Reader"
    LANGUAGES C CXX
)

# Use C++17
set(CMAKE_CXX_STANDARD 17)

# Use C99
set(CMAKE_C_STANDARD 99)

add_executable(carnage-reporter
    src/main.cpp
    src/stb/stb_impl.c
)
