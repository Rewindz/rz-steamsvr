
set(DAEMON_SRC
  "main.cpp"
  "luapi.cpp"
)

list(TRANSFORM DAEMON_SRC PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/src/")


add_executable(rzdaemon)

set_target_properties(rzdaemon PROPERTIES
  CMAKE_CXX_STANDARD 2C
  CMAKE_CXX_STANDARD_REQUIRED ON
)

target_compile_options(rzdaemon PRIVATE -std=gnu++2c -freflection)

target_sources(rzdaemon PRIVATE ${DAEMON_SRC})

target_include_directories(rzdaemon PRIVATE "${CMAKE_SOURCE_DIR}/include/")

target_link_libraries(rzdaemon PRIVATE rzutils spdlog::spdlog lua)
