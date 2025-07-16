cmake_minimum_required(VERSION 4.0.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(http_server)

file(GLOB_RECURSE SRC_FILES
  "${CMAKE_SOURCE_DIR}/main/src/*.c"
)

set(INC_DIRS
  "${CMAKE_SOURCE_DIR}/main/inc"
)

file(GLOB_RECURSE STATIC_ASSETS
  "${CMAKE_SOURCE_DIR}/main/assets/static/*"
)

set(GEN_DIR "${ZEPHYR_BINARY_DIR}/include/generated")
foreach(STATIC_ASSET ${STATIC_ASSETS})
  file(RELATIVE_PATH OUTPUT_ASSET "${CMAKE_SOURCE_DIR}/main" "${STATIC_ASSET}")
  string(CONCAT OUTPUT_ASSET "${GEN_DIR}/${OUTPUT_ASSET}" ".gz.inc")
  generate_inc_file_for_target(app "${STATIC_ASSET}" "${OUTPUT_ASSET}" "--gzip")
endforeach()

execute_process(COMMAND python ${CMAKE_SOURCE_DIR}/main/scripts/generate_static_resources.py ${CMAKE_SOURCE_DIR}/main/assets/static OUTPUT_FILE "${GEN_DIR}/http_resources.h")

zephyr_linker_sources(SECTIONS sections-rom.ld)

target_sources(app PRIVATE ${SRC_FILES})
target_include_directories(app PRIVATE ${INC_DIRS})