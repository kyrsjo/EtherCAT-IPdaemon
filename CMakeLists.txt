#
# EtherCAT-IPdaemon build file
# ===========================================
#
#
cmake_minimum_required(VERSION 2.8.12)

project(EtherCAT-IPdaemon C)

add_subdirectory(SOEM)

set(SOURCES src/EtherCatDaemon.c src/ecatDriver.c src/networkServer.c)
add_executable(daemon ${SOURCES})
target_link_libraries(daemon soem)
#install(TARGETS daemon DESTINATION bin)

#Copy the config.txt the first time cmake is ran, then leave it alone
if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/config.txt)
  #do nothing
  message("")
  message("NOTE: config.txt already copied, do not overwrite!")
  message("")
else()
  message("")
  message("NOTE: config.txt was copied from the source folder")
  message("")
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/config.txt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endif()
