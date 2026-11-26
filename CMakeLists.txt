# Alternative build with the regular VitaSDK (PC): uses VitaSDK's own crt0/newlib,
# so src/crt.c and vita.ld are NOT used here.
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()
project(VitaDeck C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaDeck")
set(VITA_TITLEID  "VLAUNCH01")
set(VITA_VERSION  "01.20")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wno-unused-function")

add_executable(launcher src/gfx.c src/img.c src/ui.c src/platform_vita.c)
target_link_libraries(launcher
  m SceDisplay_stub SceCtrl_stub SceTouch_stub SceRtc_stub ScePower_stub
  SceAppMgr_stub SceAVConfig_stub SceSysmem_stub SceIofilemgr_stub
  SceLibKernel_stub SceKernelThreadMgr_stub SceProcessmgr_stub)

vita_create_self(eboot.bin launcher UNSAFE)
vita_create_vpk(VitaDeck.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
       sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
       sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
       sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
       res/Lato-Regular.ttf res/Lato-Regular.ttf
       res/Lato-Bold.ttf res/Lato-Bold.ttf
       res/SpaceGrotesk-Regular.ttf res/SpaceGrotesk-Regular.ttf
       res/SpaceGrotesk-Bold.ttf res/SpaceGrotesk-Bold.ttf
       res/OFL-Lato.txt res/OFL-Lato.txt
       res/OFL-SpaceGrotesk.txt res/OFL-SpaceGrotesk.txt)
