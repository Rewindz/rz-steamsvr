cmake_minimum_required(VERSION 3.28)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(rzsvr LANGUAGES C CXX)

include(FetchContent)

FetchContent_Declare(rzutils
  GIT_REPOSITORY "https://github.com/Rewindz/rzutils"
  GIT_TAG "main"
  GIT_SHALLOW ON
)

FetchContent_Declare(spdlog
  GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
  GIT_TAG "v1.17.0"
  GIT_SHALLOW ON
)

FetchContent_Declare(luadl
  URL "https://www.lua.org/ftp/lua-5.5.1.tar.gz"
)

FetchContent_Declare(cpr
  GIT_REPOSITORY "https://github.com/libcpr/cpr.git"
  GIT_TAG "1.14.2"
  GIT_SHALLOW ON
)

FetchContent_Declare(argparse
  GIT_REPOSITORY "https://github.com/p-ranav/argparse.git"
  GIT_TAG "v3.2"
  GIT_SHALLOW ON
)

set(BUILD_SHARED_LIBS OFF)

FetchContent_MakeAvailable(rzutils luadl spdlog cpr argparse)


add_library(lua STATIC)
file(GLOB LUA_SRC "${luadl_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM LUA_SRC
  "${luadl_SOURCE_DIR}/src/lua.c"
  "${luadl_SOURCE_DIR}/src/luac.c"
  "${luadl_SOURCE_DIR}/src/ltests.c"
)
target_sources(lua PRIVATE ${LUA_SRC})
target_include_directories(lua PUBLIC "${luadl_SOURCE_DIR}/src")

set(PROJ_SRC
  "main.cpp"
  "steamcmd.cpp"
  "message.cpp"
)

list(TRANSFORM PROJ_SRC PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/src/")

add_executable(rzsvr)

target_sources(rzsvr PRIVATE ${PROJ_SRC})

set_target_properties(rzsvr PROPERTIES
  CMAKE_CXX_STANDARD 23
  CMAKE_CXX_STANDARD_REQUIRED ON
)

target_link_libraries(rzsvr PRIVATE
  rzutils
  spdlog::spdlog
  cpr::cpr
  argparse::argparse
)

target_include_directories(rzsvr PRIVATE "${CMAKE_SOURCE_DIR}/include")

add_subdirectory("daemon")
