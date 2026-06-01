cmake_minimum_required(VERSION 3.16)
project(Recordlab_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ECHO_CPP_DIR /home/hyren/echo_message_system/cpp-refactor)
set(ECHO_ZMQ_DIR ${ECHO_CPP_DIR}/external/zeromq)

add_library(recordlab_host
  src/agents/agent_config_loader.cpp
  src/bus/host_message_bus.cpp
  src/communication/echo_action_client.cpp
  src/communication/echo_topic_subscriber.cpp
  src/common/process_handle.cpp
)

target_include_directories(recordlab_host PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${ECHO_CPP_DIR}/include
  ${ECHO_ZMQ_DIR}/include
)

target_link_libraries(recordlab_host PUBLIC
  ${ECHO_ZMQ_DIR}/lib/libzmq.a
  pthread
)

add_executable(recordlab_cli tools/recordlab_cli.cpp)
target_link_libraries(recordlab_cli PRIVATE recordlab_host)

enable_testing()

foreach(test_name
  test_agent_config_loader
  test_host_message_bus
  test_echo_python_integration
)
  add_executable(${test_name} tests/${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE recordlab_host)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
