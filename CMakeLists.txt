cmake_minimum_required(VERSION 3.10)
project(monitor LANGUAGES CXX)

find_package(Curses REQUIRED)

add_library(monitor_core
  src/format.cpp
  src/linux_parser.cpp
  src/process.cpp
  src/processor.cpp
  src/system.cpp
)
target_include_directories(monitor_core PUBLIC include)
target_compile_features(monitor_core PUBLIC cxx_std_17)
target_compile_options(monitor_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(monitor
  src/main.cpp
  src/ncurses_display.cpp
)
target_include_directories(monitor PRIVATE ${CURSES_INCLUDE_DIRS})
target_compile_features(monitor PRIVATE cxx_std_17)
target_compile_options(monitor PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(monitor PRIVATE monitor_core ${CURSES_LIBRARIES})

include(CTest)
if(BUILD_TESTING)
  add_executable(monitor_tests tests/system_monitor_tests.cpp)
  target_compile_features(monitor_tests PRIVATE cxx_std_17)
  target_compile_options(monitor_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(monitor_tests PRIVATE monitor_core)
  add_test(NAME monitor_tests COMMAND monitor_tests)
endif()
