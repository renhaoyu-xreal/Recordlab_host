#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_ROOT="${HOST_ROOT}/third_party"
ECHO_ROOT="${ECHO_MESSAGE_SYSTEM_ROOT:-${THIRD_PARTY_ROOT}/echo_message_system}"

echo "[build] configure echo_message_system"
cmake -S "${ECHO_ROOT}/cpp-refactor" -B "${ECHO_ROOT}/cpp-refactor/build"

echo "[build] build echo_message_system"
cmake --build "${ECHO_ROOT}/cpp-refactor/build" -j"$(nproc)"

echo "[build] configure Recordlab_host"
cmake -S "${HOST_ROOT}" -B "${HOST_ROOT}/build" \
  -DRECORDLAB_THIRD_PARTY_DIR="${THIRD_PARTY_ROOT}" \
  -DECHO_MESSAGE_SYSTEM_ROOT="${ECHO_ROOT}"

echo "[build] build Recordlab_host"
cmake --build "${HOST_ROOT}/build" -j"$(nproc)"

echo "[build] done"
