cmake_minimum_required(VERSION 3.10)
project(hello_coroutines)

string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_UPPER)

if(CMAKE_BUILD_TYPE_UPPER STREQUAL "DEBUG")
    message(STATUS "Configuring for Debug build")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -gdwarf-4 -fno-omit-frame-pointer")
elseif(CMAKE_BUILD_TYPE_UPPER STREQUAL "RELEASE")
    message(STATUS "Configuring for Release build")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG -fno-omit-frame-pointer")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello_coroutines
    main.cpp
)

target_link_libraries(hello_coroutines
    pthread
)