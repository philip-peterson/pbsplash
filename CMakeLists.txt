cmake_minimum_required(VERSION 3.10)

# set the project name
project(pbsplash)

# add the executable
add_executable(pbsplash pbsplash.c)

include_directories(tfblib/include)

add_subdirectory(tfblib)

target_link_libraries(pbsplash tfb)
