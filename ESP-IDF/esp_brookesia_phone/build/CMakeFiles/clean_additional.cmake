# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "bootloader/bootloader.bin"
  "bootloader/bootloader.elf"
  "bootloader/bootloader.map"
  "config/sdkconfig.cmake"
  "config/sdkconfig.h"
  "esp-idf/esptool_py/flasher_args.json.in"
  "esp-idf/mbedtls/x509_crt_bundle"
  "esp_brookesia_demo.bin"
  "esp_brookesia_demo.map"
  "flash_app_args"
  "flash_bootloader_args"
  "flash_project_args"
  "flasher_args.json"
  "human_face_detect.espdl.S"
  "ldgen_libraries"
  "ldgen_libraries.in"
  "pedestrian_detect.espdl.S"
  "project_elf_src_esp32p4.c"
  "storage.bin"
  "x509_crt_bundle.S"
  )
endif()
