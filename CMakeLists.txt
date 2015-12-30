cmake_minimum_required(VERSION 3.3)
project(cmplayer)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
        main.cpp)

set(BOOST_LIBRARYDIR
        /usr/lib/x86_64-linux-gnu)
set(Boost_LIBARY_DEBUG
        /usr/lib/x86_64-linux-gnu)

find_package(Boost ) #1.55 COMPONENTS program_options REQUIRED
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    add_executable(cmplayer ${SOURCE_FILES})
    target_link_libraries(cmplayer ${Boost_PROGRAM_OPTIONS_LIBRARY_RELEASE})
endif()
