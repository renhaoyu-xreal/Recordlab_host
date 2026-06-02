#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_ROOT="${HOST_ROOT}/third_party"
ECHO_ROOT="${ECHO_MESSAGE_SYSTEM_ROOT:-${THIRD_PARTY_ROOT}/echo_message_system}"

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
