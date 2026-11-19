# BrainOS STT spike: one small ESP-IDF app for the Seeed reTerminal Sticky that records from the
# PDM microphone and transcribes with one of two on-chip English engines, chosen at build time:
#   STICKY_ENGINE=oido     OIDO_DIR=/path/to/oido         idf.py build
#   STICKY_ENGINE=babytalk BABYTALK_DIR=/path/to/babytalk idf.py build
cmake_minimum_required(VERSION 3.16)
if("$ENV{STICKY_ENGINE}" STREQUAL "oido")
  set(EXTRA_COMPONENT_DIRS "${OIDO_DIR}/esp32/components")
  add_compile_definitions(ENGINE_OIDO TASR_SINGLE_CORE)
elseif("$ENV{STICKY_ENGINE}" STREQUAL "babytalk")
  set(EXTRA_COMPONENT_DIRS "${BABYTALK_DIR}/components")
  add_compile_definitions(ENGINE_BABYTALK)
else()
  message(FATAL_ERROR "set STICKY_ENGINE=oido or STICKY_ENGINE=babytalk in the environment")
endif()
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sticky_stt)
