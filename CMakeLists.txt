cmake_minimum_required(VERSION 3.16)
project(Recordlab_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(RECORDLAB_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "RecordLab third-party dependency root")
set(ECHO_MESSAGE_SYSTEM_ROOT "${RECORDLAB_THIRD_PARTY_DIR}/echo_message_system" CACHE PATH "echo_message_system repository root")
set(ECHO_CPP_DIR "${ECHO_MESSAGE_SYSTEM_ROOT}/cpp-refactor" CACHE PATH "echo_message_system C++ source root")
set(ECHO_ZMQ_DIR ${ECHO_CPP_DIR}/external/zeromq)

if(NOT EXISTS "${ECHO_CPP_DIR}/include")
  message(FATAL_ERROR
    "echo_message_system not found at ${ECHO_CPP_DIR}. "
    "Run host_scripts/install_dependencies.sh or set -DECHO_CPP_DIR=/path/to/echo_message_system/cpp-refactor.")
endif()

if(NOT EXISTS "${ECHO_ZMQ_DIR}/lib/libzmq.a")
  message(FATAL_ERROR
    "ZeroMQ static library not found at ${ECHO_ZMQ_DIR}/lib/libzmq.a. "
    "Run host_scripts/install_dependencies.sh after cloning echo_message_system.")
endif()

add_library(recordlab_host
  include/recordlab_host/common/logger.h
  include/recordlab_host/scripts/scripts_actuator.h
  include/recordlab_host/ui/data_page.h
  include/recordlab_host/ui/entry_page.h
  include/recordlab_host/ui/imu_runtime_bridge.h
  include/recordlab_host/ui/main_window.h
  include/recordlab_host/ui/script_page.h
  include/recordlab_host/ui/sensor_workspace_widget.h
  include/recordlab_host/ui/workspace_page.h
  ${ECHO_CPP_DIR}/src/action.cpp
  ${ECHO_CPP_DIR}/src/context.cpp
  ${ECHO_CPP_DIR}/src/master.cpp
  ${ECHO_CPP_DIR}/src/parameter_server.cpp
  ${ECHO_CPP_DIR}/src/publisher.cpp
  ${ECHO_CPP_DIR}/src/service.cpp
  ${ECHO_CPP_DIR}/src/subscriber.cpp
  src/agents/agent_config_loader.cpp
  src/agents/agent_manager.cpp
  src/bus/host_message_bus.cpp
  src/communication/echo_action_client.cpp
  src/communication/echo_topic_subscriber.cpp
  src/common/logger.cpp
  src/common/process_handle.cpp
  src/data/sensor_queue.cpp
  src/data/data_receiver.cpp
  src/scripts/scripts_actuator.cpp
  src/ui/data_page.cpp
  src/ui/entry_page.cpp
  src/ui/imu_runtime_bridge.cpp
  src/ui/main_window.cpp
  src/ui/script_page.cpp
  src/ui/sensor_workspace_widget.cpp
  src/ui/workspace_page.cpp
)

target_include_directories(recordlab_host PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${ECHO_CPP_DIR}/include
  ${ECHO_ZMQ_DIR}/include
)

target_link_libraries(recordlab_host PUBLIC
  ${ECHO_ZMQ_DIR}/lib/libzmq.a
  Qt6::Widgets
  pthread
)

add_executable(recordlab_cli app/recordlab_cli.cpp)
target_link_libraries(recordlab_cli PRIVATE recordlab_host)

add_executable(recordlab_host_app app/recordlab_host_app.cpp)
target_link_libraries(recordlab_host_app PRIVATE recordlab_host)

enable_testing()

foreach(test_name
  test_agent_config_loader
  test_echo_topic_encoding
  test_host_message_bus
  test_echo_python_integration
  test_imu_runtime_bridge
  test_ui_layout_contract
)
  add_executable(${test_name} tests/${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE recordlab_host)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

set_tests_properties(test_ui_layout_contract PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
