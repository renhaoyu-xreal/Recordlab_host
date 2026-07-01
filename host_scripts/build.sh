#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_ROOT="${HOST_ROOT}/third_party"
ECHO_ROOT="${ECHO_MESSAGE_SYSTEM_ROOT:-${THIRD_PARTY_ROOT}/echo_message_system}"
DEFAULT_ECHO_BUILD_DIR="${ECHO_ROOT}/cpp-refactor/build"
DEFAULT_HOST_BUILD_DIR="${HOST_ROOT}/build"

if [[ "${EUID}" -eq 0 ]]; then
  echo "[build] do not run host_scripts/build.sh with sudo." >&2
  echo "[build] build as your normal user to avoid root-owned files in the workspace." >&2
  exit 1
fi

cmake_clean_env() {
  env \
    -u QT_HOST_PATH \
    -u QT_HOST_PATH_CMAKE_DIR \
    -u Qt6_DIR \
    -u Qt6Widgets_DIR \
    -u Qt6Core_DIR \
    -u Qt6Gui_DIR \
    "$@"
}

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
  echo "[build] run host_scripts/install_dependencies.sh first, or install libzmq3-dev manually" >&2
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

  if cmake_clean_env cmake -S "${tmp_dir}" -B "${tmp_dir}/build" >/dev/null 2>"${tmp_dir}/cmake.err"; then
    rm -rf "${tmp_dir}"
    return 0
  fi

  echo "[build] Qt6 Widgets CMake package is not usable on this machine" >&2
  echo "[build] sanitized env: QT_HOST_PATH='${QT_HOST_PATH:-}', QT_HOST_PATH_CMAKE_DIR='${QT_HOST_PATH_CMAKE_DIR:-}', Qt6_DIR='${Qt6_DIR:-}'" >&2
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

select_build_dir() {
  local requested_dir="$1"
  local label="$2"

  if [[ -d "${requested_dir}" ]]; then
    if [[ -w "${requested_dir}" ]]; then
      printf '%s\n' "${requested_dir}"
      return 0
    fi
  else
    local parent_dir
    parent_dir="$(dirname "${requested_dir}")"
    if [[ -d "${parent_dir}" && -w "${parent_dir}" ]]; then
      printf '%s\n' "${requested_dir}"
      return 0
    fi
  fi

  local fallback_dir="${HOST_ROOT}/.recordlab-build/${label}"
  mkdir -p "${fallback_dir}"
  echo "[build] ${label} build dir is not writable, using fallback: ${fallback_dir}" >&2
  printf '%s\n' "${fallback_dir}"
}

require_zmq_hpp
require_qt6_widgets

ECHO_BUILD_DIR="$(select_build_dir "${RECORDLAB_ECHO_BUILD_DIR:-${DEFAULT_ECHO_BUILD_DIR}}" "echo_message_system")"
HOST_BUILD_DIR="$(select_build_dir "${RECORDLAB_HOST_BUILD_DIR:-${DEFAULT_HOST_BUILD_DIR}}" "host")"

reset_stale_cmake_cache "${ECHO_ROOT}/cpp-refactor" "${ECHO_BUILD_DIR}" "echo_message_system"

echo "[build] configure echo_message_system"
cmake_clean_env cmake -S "${ECHO_ROOT}/cpp-refactor" -B "${ECHO_BUILD_DIR}"

echo "[build] build echo_message_system"
cmake_clean_env cmake --build "${ECHO_BUILD_DIR}" -j"$(nproc)"

reset_stale_cmake_cache "${HOST_ROOT}" "${HOST_BUILD_DIR}" "Recordlab_host"

BUILD_TESTING_MODE="${RECORDLAB_BUILD_TESTING:-auto}"
if [[ "${BUILD_TESTING_MODE}" == "auto" ]]; then
  if [[ -d "${HOST_ROOT}/tests" ]]; then
    BUILD_TESTING_MODE="ON"
  else
    BUILD_TESTING_MODE="OFF"
  fi
fi

echo "[build] Recordlab_host BUILD_TESTING=${BUILD_TESTING_MODE}"

echo "[build] configure Recordlab_host"
cmake_clean_env cmake -S "${HOST_ROOT}" -B "${HOST_BUILD_DIR}" \
  -DRECORDLAB_THIRD_PARTY_DIR="${THIRD_PARTY_ROOT}" \
  -DECHO_MESSAGE_SYSTEM_ROOT="${ECHO_ROOT}" \
  -DBUILD_TESTING="${BUILD_TESTING_MODE}"

echo "[build] build Recordlab_host"
cmake_clean_env cmake --build "${HOST_BUILD_DIR}" -j"$(nproc)"

if [[ -d "${HOST_ROOT}/bin" ]]; then
  install -Dm755 "${HOST_BUILD_DIR}/recordlab_host_app" "${HOST_ROOT}/bin/recordlab_host_app"
  install -Dm755 "${HOST_BUILD_DIR}/recordlab_cli" "${HOST_ROOT}/bin/recordlab_cli"
fi

echo "[build] done"
