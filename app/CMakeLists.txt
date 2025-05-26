cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_sources(
  app
  PRIVATE
    src/audio_data.c
    src/main.c
    src/ble.c
    src/buttons.c
    src/charger.c
    src/display.c
    src/flash.c
    src/haptic.c
    src/imu.c
    src/light.c
    src/mag.c
    src/mic.c
    src/press.c
    src/speaker.c
)

if (CONFIG_BOARD_ASTERIX)
  target_sources(
    app
    PRIVATE
      src/lfxo.c
  )
endif()
