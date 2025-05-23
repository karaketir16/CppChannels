cmake_minimum_required(VERSION 3.10)

# Project name
project(ChannelLibrary VERSION 1.0.0 LANGUAGES CXX)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Define the header-only library
add_library(ChannelLib INTERFACE)

# Include the header file directory
target_include_directories(ChannelLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Add Google Test
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# Add the test executable
add_executable(channel_test tests/channel_test.cpp)
target_link_libraries(channel_test ChannelLib ${GTEST_LIBRARIES} pthread)

add_executable(move_copy_test tests/move_copy_test.cpp)
target_link_libraries(move_copy_test ChannelLib ${GTEST_LIBRARIES} pthread) 
