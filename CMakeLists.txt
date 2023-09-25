cmake_minimum_required(VERSION 3.0.0)
project(
  TgReminderBot
  VERSION 0.1.0
  LANGUAGES C CXX)

find_package(Boost # COMPONENTS libboost_algorithm
)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/3rdparty/tgbot-cpp")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/3rdparty/fmt")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/3rdparty/json")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/3rdparty/date")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/3rdparty/unqlite_cpp")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/3rdparty/spdlog")

add_definitions(-DUNQLITE_CPP_ALLOW_EXCEPTIONS)

set(${CMAKE_CXX_FLAGS} "-I${CMAKE_CURRENT_LIST_DIR}/src")

file(
  GLOB_RECURSE FIT_LOG_SOURCES
  RELATIVE "${CMAKE_CURRENT_LIST_DIR}"
  CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/src/*.[ch]pp")

add_executable(
  TgReminderBot
  ${FIT_LOG_SOURCES} "${CMAKE_CURRENT_LIST_DIR}/3rdparty/date/src/tz.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/3rdparty/unqlite/unqlite.c")

target_link_libraries(TgReminderBot PUBLIC m)

target_include_directories(TgReminderBot
                           PUBLIC "${CMAKE_CURRENT_LIST_DIR}/3rdparty/unqlite")
target_include_directories(TgReminderBot PUBLIC "${CMAKE_CURRENT_LIST_DIR}/src")

target_link_libraries(
  TgReminderBot
  PUBLIC TgBot
         fmt::fmt
         nlohmann_json::nlohmann_json
         date::date
         unqlite_cpp::unqlite_cpp
         spdlog::spdlog_header_only
         ${Boost_LIBRARIES})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
