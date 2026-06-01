#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_ROOT="${HOST_ROOT}/third_party"
ECHO_REPO_URL="${ECHO_MESSAGE_SYSTEM_REPO_URL:-https://github.com/renhaoyu-xreal/echo_message_system.git}"
NODES_REPO_URL="${RECORDLAB_NODES_REPO_URL:-https://github.com/renhaoyu-xreal/Recordlab_nodes.git}"
ECHO_ROOT="${THIRD_PARTY_ROOT}/echo_message_system"
NODES_ROOT="${THIRD_PARTY_ROOT}/Recordlab_nodes"
VENV_DIR="${RECORDLAB_VENV_DIR:-${HOST_ROOT}/.venv-py310}"
PYTHON_BIN="${PYTHON_BIN:-python3.10}"

echo "[recordlab] dependency bootstrap"
echo "[recordlab] host root: ${HOST_ROOT}"

install_apt_packages() {
  if ! command -v apt-get >/dev/null 2>&1; then
    return
  fi

  local missing=()
  for cmd in git cmake pkg-config g++; do
    if ! command -v "${cmd}" >/dev/null 2>&1; then
      missing+=("${cmd}")
    fi
  done

  if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
    missing+=("${PYTHON_BIN}" "${PYTHON_BIN}-venv" "${PYTHON_BIN}-dev")
  fi

  if ! dpkg -s qt6-base-dev >/dev/null 2>&1; then
    missing+=("qt6-base-dev")
  fi

  if ((${#missing[@]} == 0)); then
    return
  fi

  echo "[recordlab] installing system packages: ${missing[*]}"
  sudo apt-get update
  sudo apt-get install -y git cmake build-essential pkg-config qt6-base-dev \
    "${PYTHON_BIN}" "${PYTHON_BIN}-venv" "${PYTHON_BIN}-dev" python3-pip
}

clone_or_update() {
  local url="$1"
  local dir="$2"
  if [[ -d "${dir}/.git" ]]; then
    echo "[recordlab] updating ${dir}"
    git -C "${dir}" pull --ff-only
    return
  fi
  if [[ -e "${dir}" ]]; then
    echo "[recordlab] using existing ${dir}"
    return
  fi
  echo "[recordlab] cloning ${url}"
  git clone "${url}" "${dir}"
}

install_apt_packages
mkdir -p "${THIRD_PARTY_ROOT}"
clone_or_update "${ECHO_REPO_URL}" "${ECHO_ROOT}"
clone_or_update "${NODES_REPO_URL}" "${NODES_ROOT}"

if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
  echo "[recordlab] ${PYTHON_BIN} is required. Install it or run with PYTHON_BIN=/path/to/python3.10." >&2
  exit 1
fi

if [[ ! -d "${VENV_DIR}" ]]; then
  echo "[recordlab] creating Python 3.10 venv: ${VENV_DIR}"
  "${PYTHON_BIN}" -m venv "${VENV_DIR}"
fi

RECORDLAB_PYTHON_BIN="${VENV_DIR}/bin/python"
"${RECORDLAB_PYTHON_BIN}" -m pip install --upgrade pip setuptools wheel

if [[ -f "${ECHO_ROOT}/python/setup.py" ]]; then
  echo "[recordlab] installing echo_message_system Python package"
  "${RECORDLAB_PYTHON_BIN}" -m pip install -e "${ECHO_ROOT}/python"
fi

if [[ -f "${NODES_ROOT}/pyproject.toml" ]]; then
  echo "[recordlab] installing Recordlab_nodes Python package"
  "${RECORDLAB_PYTHON_BIN}" -m pip install -e "${NODES_ROOT}"
fi

XREAL_WHL="${THIRD_PARTY_ROOT}/xreal_glasses/xreal_glasses-0.4.3-py3-none-any.whl"
if [[ -f "${XREAL_WHL}" ]]; then
  echo "[recordlab] installing xreal_glasses wheel"
  "${RECORDLAB_PYTHON_BIN}" -m pip install "${XREAL_WHL}"
else
  echo "[recordlab] warning: xreal wheel not found at ${XREAL_WHL}" >&2
fi

if [[ ! -f "${ECHO_ROOT}/cpp-refactor/external/zeromq/lib/libzmq.a" ]]; then
  echo "[recordlab] missing vendored ZeroMQ library under ${ECHO_ROOT}/cpp-refactor/external/zeromq" >&2
  exit 1
fi

echo "[recordlab] configuring and building host"
cmake -S "${HOST_ROOT}" -B "${HOST_ROOT}/build" \
  -DRECORDLAB_THIRD_PARTY_DIR="${THIRD_PARTY_ROOT}" \
  -DECHO_MESSAGE_SYSTEM_ROOT="${ECHO_ROOT}"
cmake --build "${HOST_ROOT}/build" -j"$(nproc)"

echo "[recordlab] done. Start UI with host_scripts/start_recordlab.sh"
