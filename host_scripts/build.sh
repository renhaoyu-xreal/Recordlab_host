#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_ROOT="${HOST_ROOT}/third_party"
ECHO_ROOT="${ECHO_MESSAGE_SYSTEM_ROOT:-${THIRD_PARTY_ROOT}/echo_message_system}"

require_zmq_hpp() {
  local candidates=(
    "${ECHO_ROOT}/cpp-refactor/external/zeromq/include/zmq.hpp"
    "/usr/include/zmq.hpp"
    "/usr/local/include/zmq.hpp"
  )
  local candidate
  for candidate in "${candidates[@]}"; do
    if [[ -f "${candidate}" ]]; then
      return 0
    fi
  done

  echo "[build] missing C++ ZeroMQ header zmq.hpp" >&2
  echo "[build] run host_scripts/install_dependencies.sh first, or install cppzmq-dev manually" >&2
  exit 1
}

require_qt6_widgets() {
  local tmp_dir
  tmp_dir="$(mktemp -d)"
  cat > "${tmp_dir}/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(recordlab_qt_check LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
EOF

  if cmake -S "${tmp_dir}" -B "${tmp_dir}/build" >/dev/null 2>"${tmp_dir}/cmake.err"; then
    rm -rf "${tmp_dir}"
    return 0
  fi

  echo "[build] Qt6 Widgets CMake package is not usable on this machine" >&2
  echo "[build] run host_scripts/install_dependencies.sh first" >&2
  sed -n '1,80p' "${tmp_dir}/cmake.err" >&2 || true
  rm -rf "${tmp_dir}"
  exit 1
}

reset_stale_cmake_cache() {
  local source_dir="$1"
  local build_dir="$2"
  local label="$3"
  local cache_file="${build_dir}/CMakeCache.txt"

  if [[ ! -f "${cache_file}" ]]; then
    return
  fi

  local expected_source
  local cached_source
  expected_source="$(cd "${source_dir}" && pwd -P)"
  cached_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${cache_file}" | tail -n 1)"

  if [[ "${cached_source}" != "${expected_source}" ]]; then
    echo "[build] removing stale ${label} CMake cache"
    echo "[build]   cached: ${cached_source}"
    echo "[build]   current: ${expected_source}"
    rm -rf "${build_dir:?}"
  fi
}

require_zmq_hpp
require_qt6_widgets

reset_stale_cmake_cache "${ECHO_ROOT}/cpp-refactor" "${ECHO_ROOT}/cpp-refactor/build" "echo_message_system"

echo "[build] configure echo_message_system"
cmake -S "${ECHO_ROOT}/cpp-refactor" -B "${ECHO_ROOT}/cpp-refactor/build"

echo "[build] build echo_message_system"
cmake --build "${ECHO_ROOT}/cpp-refactor/build" -j"$(nproc)"

reset_stale_cmake_cache "${HOST_ROOT}" "${HOST_ROOT}/build" "Recordlab_host"

echo "[build] configure Recordlab_host"
cmake -S "${HOST_ROOT}" -B "${HOST_ROOT}/build" \
  -DRECORDLAB_THIRD_PARTY_DIR="${THIRD_PARTY_ROOT}" \
  -DECHO_MESSAGE_SYSTEM_ROOT="${ECHO_ROOT}"

echo "[build] build Recordlab_host"
cmake --build "${HOST_ROOT}/build" -j"$(nproc)"

echo "[build] done"
