#
# EtherCAT-IPdaemon build file
# ===========================================
#
#
cmake_minimum_required(VERSION 2.8.12)

project(EtherCAT-IPdaemon C)

add_subdirectory(SOEM)

set(SOURCES src/daemon.c)
add_executable(daemon ${SOURCES})
target_link_libraries(daemon soem)
#install(TARGETS daemon DESTINATION bin)

#add_subdirectory(src)
