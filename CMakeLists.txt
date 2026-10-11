cmake_minimum_required(VERSION 3.24)
project(PCGalaxyVR VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DECOMP ${CMAKE_SOURCE_DIR}/decomp)

if(ANDROID)
  set(PORT_PLATFORM android)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(PORT_PLATFORM linux)
else()
  message(FATAL_ERROR "PCGalaxyVR builds for Linux (and, from the GalaxyQuest sources, Android); Windows is planned")
endif()

if(PORT_PLATFORM STREQUAL "linux")
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "PCGalaxyVR builds with clang (CC=clang CXX=clang++): the decompilation relies on its extensions")
  endif()
  # libc++, as with the Android NDK: libstdc++ still keeps C++98's std::mem_fun,
  # binder2nd and the like, which the decompilation's MSL headers define again.
  # Linked statically, so the program needs no libc++ installed.  lld places the
  # program at its fixed address (see below).
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
  add_link_options(-stdlib=libc++ -static-libstdc++ -fuse-ld=lld)
  option(PCGVR_SYSTEM_OPENXR "Use the system's OpenXR SDK (1.1 or newer) instead of fetching it" OFF)
endif()

# ---------------------------------------------------------------------------
# Decompiled game sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE GAME_SRCS CONFIGURE_DEPENDS ${DECOMP}/src/Game/*.cpp ${DECOMP}/src/Game/*.c)
file(GLOB_RECURSE JSYSTEM_SRCS CONFIGURE_DEPENDS ${DECOMP}/src/JSystem/*.cpp)
file(GLOB_RECURSE NW4R_SRCS CONFIGURE_DEPENDS ${DECOMP}/src/nw4r/*.cpp)

# SDK libraries that are pure software and are compiled from the decomp as-is.
set(SDK_MODULES gx gd mem arc tpl aralt)
set(SDK_SRCS)
foreach(m ${SDK_MODULES})
  file(GLOB_RECURSE _s CONFIGURE_DEPENDS ${DECOMP}/src/RVL_SDK/${m}/*.c)
  list(APPEND SDK_SRCS ${_s})
endforeach()

list(FILTER SDK_SRCS EXCLUDE REGEX "/gx/GXFifo\\.c$|/gx/GXPerf\\.c$")
# OS pieces that are pure software on top of the thread API.
list(APPEND SDK_SRCS ${DECOMP}/src/RVL_SDK/os/OSMutex.c ${DECOMP}/src/RVL_SDK/os/OSMessage.c)
# THP movie audio is plain C (the video decoder is replaced by platform/src/sdk/thp_video.cpp).
list(APPEND SDK_SRCS ${DECOMP}/src/RVL_SDK/thp/THPAudio.c)

set(DECOMP_INCLUDES
  ${CMAKE_SOURCE_DIR}/platform/include
  ${DECOMP}/include
  ${DECOMP}/libs/JSystem/include
  ${DECOMP}/libs/RVL_SDK/include
  ${DECOMP}/libs/nw4r/include
  ${DECOMP}/libs/RVLFaceLib/include
)

set(PORT_DEFS TARGET_PC=1 NDEBUG=1 VERSION=0)

# Paths in the binaries (__FILE__, debug info) relative to the repository
# and the NDK, not the machine that built them.
add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}/= -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
if(ANDROID)
  add_compile_options(-ffile-prefix-map=${ANDROID_NDK}=ndk)
endif()

set(PORT_COMMON_FLAGS
  -fshort-wchar
  -fsigned-char
  -fno-strict-aliasing
  -fwrapv
  -ffp-contract=on
  -Wno-register
  -Wno-multichar
  -Wno-invalid-offsetof
  -Wno-c++11-narrowing
  -Wno-writable-strings
  -Wno-null-conversion
  -Wno-deprecated-declarations
  -Wno-int-to-pointer-cast
  -Wno-pointer-to-int-cast
  -Wno-invalid-source-encoding
  -Wno-unused-value
  -Wno-return-type
  -Wno-switch
  -Wno-parentheses
  -Wno-dangling-else
  -Wno-tautological-compare
  -Wno-tautological-undefined-compare
  -Wno-undefined-bool-conversion
  -Wno-constant-conversion
  -Wno-shift-negative-value
  -Wno-implicit-function-declaration
  -Wno-int-conversion
  -Wno-incompatible-pointer-types
  -Wno-incompatible-function-pointer-types
  -Wno-pointer-sign
  -Wno-unknown-pragmas
  -Wno-ignored-attributes
  -Wno-microsoft-template
  -fms-extensions
  -Wno-void-pointer-to-int-cast
  -Wno-microsoft-cast
  -Wno-microsoft-anon-tag
  -Wno-microsoft-include
  -Wno-language-extension-token
  -ferror-limit=0
  -include ${CMAKE_SOURCE_DIR}/platform/include/port/compat.h
)
set(PORT_CXX_FLAGS -fno-exceptions -fno-rtti)
set(PORT_C_FLAGS)
if(PORT_PLATFORM STREQUAL "linux")
  # Position-independent code in a program linked at a fixed address above
  # 2 GB: nothing may need a 32-bit absolute relocation.
  list(APPEND PORT_COMMON_FLAGS -fPIE)
endif()

add_library(decomp OBJECT ${GAME_SRCS} ${JSYSTEM_SRCS} ${NW4R_SRCS} ${SDK_SRCS})
target_include_directories(decomp PRIVATE ${DECOMP_INCLUDES})
target_compile_definitions(decomp PRIVATE ${PORT_DEFS})
target_compile_options(decomp PRIVATE ${PORT_COMMON_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:${PORT_CXX_FLAGS}> $<$<COMPILE_LANGUAGE:C>:${PORT_C_FLAGS}>)

# Flags for the port's own C and C++ code (not its one assembly file).
set(PORT_CODE_FLAGS
  $<$<COMPILE_LANGUAGE:C,CXX>:${PORT_COMMON_FLAGS}>
  $<$<COMPILE_LANGUAGE:CXX>:${PORT_CXX_FLAGS}>
  $<$<COMPILE_LANGUAGE:C>:${PORT_C_FLAGS}>)

# ---------------------------------------------------------------------------
# Port platform layer + final game library
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PLATFORM_SRCS CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/platform/src/*.c
  ${CMAKE_SOURCE_DIR}/platform/src/*.cpp)

if(ANDROID)
  add_library(game SHARED $<TARGET_OBJECTS:decomp> ${PLATFORM_SRCS})
  target_include_directories(game PRIVATE ${DECOMP_INCLUDES})
  target_compile_definitions(game PRIVATE ${PORT_DEFS} PORT_PLATFORM_CODE=1)
  target_compile_options(game PRIVATE ${PORT_CODE_FLAGS})
  # -Bsymbolic: game code must bind to the port's operator new/delete, not the
  # system C++ runtime's.
  target_link_options(game PRIVATE -Wl,--no-undefined -Wl,--error-limit=0 -Wl,-Bsymbolic -Wl,--gc-sections)
  target_link_libraries(game PRIVATE log android)

  # The Khronos OpenXR loader (tools/fetch_openxr.sh puts it there).
  set(OPENXR_DIR ${CMAKE_SOURCE_DIR}/third_party/openxr/prefab/modules)
  set(GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)

  add_library(openxr_loader SHARED IMPORTED)
  set_target_properties(openxr_loader PROPERTIES IMPORTED_LOCATION ${OPENXR_DIR}/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so)

  target_include_directories(game PRIVATE ${OPENXR_DIR}/headers/include ${GLUE_DIR})
  target_link_libraries(game PRIVATE openxr_loader EGL GLESv3 aaudio)

  # Launcher: reserves the 32-bit game window and loads libgame.so into it.
  add_library(main SHARED platform/android/launcher.c ${GLUE_DIR}/android_native_app_glue.c)
  target_include_directories(main PRIVATE ${GLUE_DIR})
  target_link_libraries(main PRIVATE android log dl)
  target_link_options(main PRIVATE -u ANativeActivity_onCreate)

  # Preloaded into the app process by wrap.sh: reserves the game window before
  # ART lays out its heap.
  add_library(reserve SHARED platform/android/reserve.c)
  target_link_libraries(reserve PRIVATE log)

  # Bring-up tool: boots libgame.so from an adb shell without VR.
  add_executable(petari_headless platform/common/headless.c)
  target_link_libraries(petari_headless PRIVATE log dl m)
  target_include_directories(petari_headless PRIVATE platform/include)
else()
  # ---------------------------------------------------------------------------
  # Linux: one program, pcgalaxyvr, the headset app and the headless runner.
  # ---------------------------------------------------------------------------
  find_package(SDL2 REQUIRED)
  find_package(OpenGL REQUIRED COMPONENTS OpenGL GLX EGL)
  find_package(X11 REQUIRED)
  find_package(Threads REQUIRED)

  if(PCGVR_SYSTEM_OPENXR)
    find_package(OpenXR 1.1 CONFIG REQUIRED)
  else()
    # The Khronos OpenXR SDK (the headers have the Meta extensions the port
    # asks for; the loader is linked in statically).
    include(FetchContent)
    set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_WITH_SYSTEM_JSONCPP OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(openxr
      GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
      GIT_TAG release-1.1.63
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(openxr)
  endif()

  add_executable(pcgalaxyvr $<TARGET_OBJECTS:decomp> ${PLATFORM_SRCS}
    platform/common/headless.c
    platform/linux/main.cpp
    platform/linux/flat_app.cpp
    platform/linux/window.S)
  target_include_directories(pcgalaxyvr PRIVATE ${DECOMP_INCLUDES})
  target_compile_definitions(pcgalaxyvr PRIVATE ${PORT_DEFS} PORT_PLATFORM_CODE=1 PCGVR_VERSION="${PROJECT_VERSION}")
  target_compile_options(pcgalaxyvr PRIVATE ${PORT_CODE_FLAGS})
  # The game keeps pointers in 32-bit fields: the program is linked at
  # 0x98000000 (not position-independent), and window.ld places the address
  # window above it (see platform/linux/main.cpp).
  # The C runtime's startup files for programs that are not position
  # independent (crt1.o, crtbegin.o) address their data with 32-bit absolute
  # relocations, which cannot reach 0x98000000: the position-independent ones
  # are linked instead, crtendS.o and crtn.o last on the line.
  foreach(crt Scrt1 crti crtbeginS crtendS crtn)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=${crt}.o OUTPUT_VARIABLE CRT_${crt} OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT IS_ABSOLUTE "${CRT_${crt}}")
      message(FATAL_ERROR "cannot find the C runtime's ${crt}.o (clang -print-file-name=${crt}.o)")
    endif()
  endforeach()
  set(CMAKE_CXX_STANDARD_LIBRARIES "${CRT_crtendS} ${CRT_crtn}")
  target_link_options(pcgalaxyvr PRIVATE -no-pie -nostartfiles ${CRT_Scrt1} ${CRT_crti} ${CRT_crtbeginS}
    -Wl,--image-base=0x98000000 -Wl,-T,${CMAKE_SOURCE_DIR}/platform/linux/window.ld -Wl,--error-limit=0)
  set_property(TARGET pcgalaxyvr APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/platform/linux/window.ld)
  target_link_libraries(pcgalaxyvr PRIVATE OpenXR::openxr_loader SDL2::SDL2 OpenGL::OpenGL OpenGL::GLX OpenGL::EGL X11::X11
    Threads::Threads ${CMAKE_DL_LIBS} m)
  install(TARGETS pcgalaxyvr RUNTIME DESTINATION bin)
endif()
