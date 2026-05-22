cmake_minimum_required(VERSION 3.16)
project(Recordlab_master CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
find_package(Threads REQUIRED)

option(BUILD_RECORDLAB_MASTER_TOOLS "Build Recordlab master tools" ON)
option(BUILD_RECORDLAB_MASTER_TESTS "Build Recordlab master tests" ON)

add_library(recordlab_master
  src/master_server.cpp
  src/master_client.cpp
  src/name_resolver.cpp
  src/script_runner.cpp
  src/shm_ring_buffer.cpp
  src/transport.cpp
)

target_include_directories(recordlab_master PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  /home/hyren/RecordLab/third_party/echo_message_system/cpp-refactor/include
  ${ZMQ_INCLUDE_DIRS}
)
target_link_libraries(recordlab_master PUBLIC ${ZMQ_LIBRARIES} Threads::Threads rt)
target_compile_definitions(recordlab_master PUBLIC
  RECORDLAB_MASTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(BUILD_RECORDLAB_MASTER_TOOLS)
  add_executable(recordlab_master_bin tools/recordlab_master_main.cpp)
  target_link_libraries(recordlab_master_bin PRIVATE recordlab_master)
  set_target_properties(recordlab_master_bin PROPERTIES OUTPUT_NAME recordlab_master)

  add_executable(recordlab_master_cli tools/recordlab_master_cli.cpp)
  target_link_libraries(recordlab_master_cli PRIVATE recordlab_master)

  add_executable(recordlab_script_runner tools/recordlab_script_runner.cpp)
  target_link_libraries(recordlab_script_runner PRIVATE recordlab_master)
endif()

if(BUILD_RECORDLAB_MASTER_TESTS)
  enable_testing()
  foreach(t
    test_name_resolver
    test_registries
    test_master_client
    test_graph_events
    test_action_req_rep_recovery
    test_shm_ring_buffer
    test_script_runner
  )
    add_executable(${t} test/${t}.cpp)
    target_link_libraries(${t} PRIVATE recordlab_master)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()
