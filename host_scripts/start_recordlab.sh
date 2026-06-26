#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_ROOT="${HOST_ROOT}/third_party"
ECHO_ROOT="${ECHO_MESSAGE_SYSTEM_ROOT:-${THIRD_PARTY_ROOT}/echo_message_system}"
NODES_ROOT="${RECORDLAB_NODES_ROOT:-${THIRD_PARTY_ROOT}/Recordlab_nodes}"
AGENTS_CONFIG="${RECORDLAB_AGENTS_CONFIG:-${NODES_ROOT}/config/agents_config.json}"
APP_BIN="${HOST_ROOT}/build/recordlab_host_app"
VENV_DIR="${RECORDLAB_VENV_DIR:-${HOST_ROOT}/.venv-py310}"
RUN_ID="$(date +%Y%m%d_%H%M%S)"
export RECORDLAB_LOG_DIR="${RECORDLAB_LOG_DIR:-${HOST_ROOT}/logs/recordlab_${RUN_ID}}"
export RECORDLAB_TERMINAL_TEE_ACTIVE=1
mkdir -p "${RECORDLAB_LOG_DIR}"

exec > >(tee -a "${RECORDLAB_LOG_DIR}/all.log") 2>&1

echo "[recordlab] log dir: ${RECORDLAB_LOG_DIR}"
echo "[recordlab] terminal output is mirrored to ${RECORDLAB_LOG_DIR}/all.log"

if [[ -x "${VENV_DIR}/bin/python" ]]; then
  export RECORDLAB_PYTHON_BIN="${RECORDLAB_PYTHON_BIN:-${VENV_DIR}/bin/python}"
else
  export RECORDLAB_PYTHON_BIN="${RECORDLAB_PYTHON_BIN:-python3.10}"
fi

export RECORDLAB_NODES_ROOT="${NODES_ROOT}"
export ECHO_MESSAGE_SYSTEM_ROOT="${ECHO_ROOT}"
export ECHO_MESSAGE_SYSTEM_PYTHON_ROOT="${ECHO_ROOT}/python"
export PYTHONPATH="${NODES_ROOT}:${ECHO_MESSAGE_SYSTEM_PYTHON_ROOT}:${PYTHONPATH:-}"

require_path() {
  local path="$1"
  local message="$2"
  if [[ ! -e "${path}" ]]; then
    echo "[recordlab] missing ${message}: ${path}" >&2
    echo "[recordlab] run host_scripts/install_dependencies.sh first" >&2
    exit 1
  fi
}

require_path "${NODES_ROOT}/recordlab_nodes/core/node_runtime.py" "Recordlab_nodes runtime"
require_path "${ECHO_MESSAGE_SYSTEM_PYTHON_ROOT}/message_system" "echo_message_system Python package"
require_path "${AGENTS_CONFIG}" "agents_config.json"

if ! "${RECORDLAB_PYTHON_BIN}" -c "import recordlab_nodes, message_system" >/dev/null 2>&1; then
  echo "[recordlab] Python dependencies are not installed in ${RECORDLAB_PYTHON_BIN}" >&2
  echo "[recordlab] run host_scripts/install_dependencies.sh first" >&2
  exit 1
fi

needs_rebuild=0
if [[ ! -x "${APP_BIN}" ]]; then
  needs_rebuild=1
elif find \
  "${HOST_ROOT}/CMakeLists.txt" \
  "${HOST_ROOT}/app" \
  "${HOST_ROOT}/include" \
  "${HOST_ROOT}/src" \
  -type f -newer "${APP_BIN}" | grep -q .; then
  needs_rebuild=1
fi

if [[ "${needs_rebuild}" -eq 1 ]]; then
  echo "[recordlab] host app is missing or out of date; building now"
  cmake -S "${HOST_ROOT}" -B "${HOST_ROOT}/build" \
    -DRECORDLAB_THIRD_PARTY_DIR="${THIRD_PARTY_ROOT}" \
    -DECHO_MESSAGE_SYSTEM_ROOT="${ECHO_ROOT}"
  cmake --build "${HOST_ROOT}/build" -j"$(nproc)"
fi

echo "[recordlab] cleaning old RecordLab processes"
pkill -9 -f "recordlab_master_app" 2>/dev/null || true
pkill -9 -f "recordlab_host_app" 2>/dev/null || true
pkill -9 -f "[p]ython.*-m recordlab_nodes\\.core\\.node_runtime" 2>/dev/null || true

echo "[recordlab] starting UI"
exec "${APP_BIN}" "${AGENTS_CONFIG}"
