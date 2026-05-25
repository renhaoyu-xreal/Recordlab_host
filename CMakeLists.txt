cmake_minimum_required(VERSION 3.16)
project(Recordlab_master CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
find_package(Threads REQUIRED)
find_package(Qt6 6.2 COMPONENTS Core Gui Widgets)

option(BUILD_RECORDLAB_MASTER_TOOLS "Build Recordlab master tools" ON)
option(BUILD_RECORDLAB_MASTER_TESTS "Build Recordlab master tests" ON)

add_library(recordlab_echo
  src/recordlab_echo/transport.cpp
  src/recordlab_echo/shm_ring_buffer.cpp
  src/recordlab_echo/stdio_channel.cpp
)

target_include_directories(recordlab_echo PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  /home/hyren/RecordLab/third_party/echo_message_system/cpp-refactor/include
  ${ZMQ_INCLUDE_DIRS}
)
target_link_libraries(recordlab_echo PUBLIC ${ZMQ_LIBRARIES} Threads::Threads rt)
target_compile_definitions(recordlab_echo PUBLIC
  RECORDLAB_MASTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(recordlab_core
  src/recordlab_core/master_client.cpp
  src/recordlab_core/name_resolver.cpp
  src/recordlab_core/node_base.cpp
  src/recordlab_core/script_runner.cpp
  src/recordlab_core/user_log.cpp
  src/recordlab_core/logger.cpp
)
target_include_directories(recordlab_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  /home/hyren/RecordLab/third_party/echo_message_system/cpp-refactor/include
)
target_link_libraries(recordlab_core PUBLIC recordlab_echo)
target_compile_definitions(recordlab_core PUBLIC
  RECORDLAB_MASTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(recordlab_xreal_runtime
  src/recordlab_xreal_runtime/xreal_bridge_client.cpp
  src/recordlab_xreal_runtime/xreal_device_catalog.cpp
  src/recordlab_xreal_runtime/xreal_sdk_probe.cpp
)
target_include_directories(recordlab_xreal_runtime PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(recordlab_xreal_runtime PUBLIC recordlab_echo)
target_compile_definitions(recordlab_xreal_runtime PUBLIC
  RECORDLAB_MASTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(recordlab_system_nodes EXCLUDE_FROM_ALL
  src/recordlab_system_nodes/launcher/launcher_config.cpp
  src/recordlab_system_nodes/launcher/launcher_node.cpp
  src/recordlab_system_nodes/recorder/recorder_node.cpp
  src/recordlab_system_nodes/health_monitor/health_monitor.cpp
  src/recordlab_system_nodes/lifecycle_manager/lifecycle_manager.cpp
  src/recordlab_system_nodes/watchdog/watchdog_node.cpp
)
target_include_directories(recordlab_system_nodes PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  /home/hyren/RecordLab/third_party/echo_message_system/cpp-refactor/include
)
target_link_libraries(recordlab_system_nodes PUBLIC recordlab_core recordlab_echo)
target_compile_definitions(recordlab_system_nodes PUBLIC
  RECORDLAB_MASTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(recordlab_tool_nodes EXCLUDE_FROM_ALL
  src/recordlab_tool_nodes/recordlab_gui/gui_config.cpp
)
target_include_directories(recordlab_tool_nodes PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(recordlab_tool_nodes PUBLIC recordlab_core recordlab_echo)
target_compile_definitions(recordlab_tool_nodes PUBLIC
  RECORDLAB_MASTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(recordlab_master_server
  src/recordlab_master/master_server.cpp
)
target_include_directories(recordlab_master_server PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  /home/hyren/RecordLab/third_party/echo_message_system/cpp-refactor/include
)
target_link_libraries(recordlab_master_server PUBLIC recordlab_core recordlab_echo)
target_compile_definitions(recordlab_master_server PUBLIC
  RECORDLAB_MASTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(recordlab_master INTERFACE)
target_link_libraries(recordlab_master INTERFACE recordlab_master_server recordlab_core recordlab_echo)

if(BUILD_RECORDLAB_MASTER_TOOLS)
  add_executable(recordlab_master_bin tools/recordlab_master_main.cpp)
  target_link_libraries(recordlab_master_bin PRIVATE recordlab_master_server)
  set_target_properties(recordlab_master_bin PROPERTIES OUTPUT_NAME recordlab_master)

  add_executable(recordlab_master_cli tools/recordlab_master_cli.cpp)
  target_link_libraries(recordlab_master_cli PRIVATE recordlab_core)

  add_executable(recordlab_script_runner tools/recordlab_script_runner.cpp)
  target_link_libraries(recordlab_script_runner PRIVATE recordlab_core recordlab_echo)

  foreach(bin health_monitor lifecycle_manager recorder_node watchdog_node recordlab_launcher)
    add_executable(${bin} tools/${bin}.cpp)
    target_link_libraries(${bin} PRIVATE recordlab_system_nodes)
  endforeach()

  if(Qt6_FOUND)
    add_executable(recordlab_gui
      tools/recordlab_gui.cpp
      src/recordlab_tool_nodes/recordlab_gui/entry_page.cpp
      src/recordlab_tool_nodes/recordlab_gui/script_page.cpp
      src/recordlab_tool_nodes/recordlab_gui/command_page.cpp
      src/recordlab_tool_nodes/recordlab_gui/recordlab_gui_window.cpp
    )
    target_link_libraries(recordlab_gui PRIVATE recordlab_tool_nodes recordlab_core recordlab_echo Qt6::Core Qt6::Gui Qt6::Widgets)
  endif()
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
    test_type_registry_encoding
    test_echo_module_boundary
    test_echo_stdio_channel
    test_master_server_dependency_boundary
    test_recorder_node
    test_watchdog_node
    test_health_monitor
    test_lifecycle_manager_recovery
    test_multinode_launcher_contract
    test_gui_config
    test_gui_layout_contract
    test_install_config_paths
    test_user_log
    test_system_logger
  )
    add_executable(${t} test/${t}.cpp)
    target_link_libraries(${t} PRIVATE recordlab_master)
    if(t MATCHES "test_(recorder_node|watchdog_node|health_monitor|lifecycle_manager_recovery|multinode_launcher_contract|install_config_paths)")
      target_link_libraries(${t} PRIVATE recordlab_system_nodes)
    endif()
    if(t STREQUAL "test_gui_config")
      target_link_libraries(${t} PRIVATE recordlab_tool_nodes)
    endif()
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()
