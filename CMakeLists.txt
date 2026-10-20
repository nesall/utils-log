cmake_minimum_required(VERSION 3.16)
project(loggerTest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(logTest main.cpp utils_log/logger.hpp README.md)
target_link_libraries(logTest PRIVATE Threads::Threads)

if(MSVC)
  target_compile_options(logTest PRIVATE /W4)
else()
  target_compile_options(logTest PRIVATE -Wall -Wextra -Wpedantic)
  # -DSANITIZE=address|undefined|thread  (don't combine address and thread)
  set(SANITIZE "" CACHE STRING "Sanitizer to enable")
  if(SANITIZE)
    target_compile_options(logTest PRIVATE -fsanitize=${SANITIZE} -g)
    target_link_options(logTest PRIVATE -fsanitize=${SANITIZE})
  endif()
endif()