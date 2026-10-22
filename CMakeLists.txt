cmake_minimum_required(VERSION 3.16)

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/version.txt PROJECT_VER LIMIT_COUNT 1)
# reconfigure (and rebuild with the new version) when version.txt changes
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/version.txt)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(FireLamp)
