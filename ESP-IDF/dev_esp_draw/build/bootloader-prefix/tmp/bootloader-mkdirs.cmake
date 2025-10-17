# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/serg/esp/esp-idf-v5.5.1/components/bootloader/subproject")
  file(MAKE_DIRECTORY "/Users/serg/esp/esp-idf-v5.5.1/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "/Users/serg/Downloads/JC4880P443C_I_W/1-Demo/idf_examples/ESP-IDF/dev_esp_draw/build/bootloader"
  "/Users/serg/Downloads/JC4880P443C_I_W/1-Demo/idf_examples/ESP-IDF/dev_esp_draw/build/bootloader-prefix"
  "/Users/serg/Downloads/JC4880P443C_I_W/1-Demo/idf_examples/ESP-IDF/dev_esp_draw/build/bootloader-prefix/tmp"
  "/Users/serg/Downloads/JC4880P443C_I_W/1-Demo/idf_examples/ESP-IDF/dev_esp_draw/build/bootloader-prefix/src/bootloader-stamp"
  "/Users/serg/Downloads/JC4880P443C_I_W/1-Demo/idf_examples/ESP-IDF/dev_esp_draw/build/bootloader-prefix/src"
  "/Users/serg/Downloads/JC4880P443C_I_W/1-Demo/idf_examples/ESP-IDF/dev_esp_draw/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/serg/Downloads/JC4880P443C_I_W/1-Demo/idf_examples/ESP-IDF/dev_esp_draw/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/serg/Downloads/JC4880P443C_I_W/1-Demo/idf_examples/ESP-IDF/dev_esp_draw/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
