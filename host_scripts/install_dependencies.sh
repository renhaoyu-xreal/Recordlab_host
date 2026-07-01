#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_ROOT="${HOST_ROOT}/third_party"
ECHO_REPO_URL="${ECHO_MESSAGE_SYSTEM_REPO_URL:-https://github.com/nreal-zhouping/echo_message_system_v2.git}"
NODES_REPO_URL="${RECORDLAB_NODES_REPO_URL:-https://github.com/nreal-zhouping/Recordlab_nodes.git}"
ECHO_ROOT="${THIRD_PARTY_ROOT}/echo_message_system"
NODES_ROOT="${THIRD_PARTY_ROOT}/Recordlab_nodes"
VENV_DIR="${RECORDLAB_VENV_DIR:-${HOST_ROOT}/.venv-py310}"
PYTHON_BIN="${PYTHON_BIN:-python3.10}"

if [[ "${EUID}" -eq 0 ]]; then
  echo "[recordlab] do not run host_scripts/install_dependencies.sh with sudo." >&2
  echo "[recordlab] run it as your normal user; the script will call sudo itself for apt-get when needed." >&2
  exit 1
fi

echo "[recordlab] dependency bootstrap"
echo "[recordlab] host root: ${HOST_ROOT}"

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

DISABLED_APT_SOURCE_FILES=()

list_optional_apt_sources() {
  find /etc/apt/sources.list.d \
    -maxdepth 1 \
    \( -name "*.list" -o -name "*.sources" \) \
    -print 2>/dev/null | sort
}

restore_disabled_apt_sources() {
  local original
  local disabled_path
  for original in "${DISABLED_APT_SOURCE_FILES[@]}"; do
    disabled_path="${original}.recordlab-disabled"
    if sudo test -e "${disabled_path}"; then
      sudo mv "${disabled_path}" "${original}"
    fi
  done
  DISABLED_APT_SOURCE_FILES=()
}

disable_optional_apt_sources() {
  local file
  local disabled_any=0
  while IFS= read -r file; do
    [[ -n "${file}" ]] || continue
    sudo mv "${file}" "${file}.recordlab-disabled"
    DISABLED_APT_SOURCE_FILES+=("${file}")
    disabled_any=1
  done < <(list_optional_apt_sources)

  if [[ "${disabled_any}" -eq 1 ]]; then
    echo "[recordlab] detected broken third-party apt sources; temporarily disabling /etc/apt/sources.list.d entries"
    return 0
  fi

  return 1
}

apt_output_needs_fallback() {
  local log_file="$1"
  grep -Eqi \
    'certificate verification failed|could not handshake|部分索引文件下载失败|some index files failed to download|failed to fetch|no_pubkey|the repository .* is not signed' \
    "${log_file}"
}

run_apt_update() {
  local first_log
  local update_status=0
  first_log="$(mktemp)"

  if sudo apt-get update >"${first_log}" 2>&1; then
    if ! apt_output_needs_fallback "${first_log}"; then
      cat "${first_log}"
      rm -f "${first_log}"
      return 0
    fi
  else
    update_status=$?
  fi

  if ! disable_optional_apt_sources; then
    cat "${first_log}"
    rm -f "${first_log}"
    return "${update_status}"
  fi

  local retry_log
  retry_log="$(mktemp)"
  if sudo apt-get update >"${retry_log}" 2>&1; then
    cat "${retry_log}"
    rm -f "${first_log}" "${retry_log}"
    return 0
  fi

  cat "${retry_log}" >&2
  rm -f "${first_log}" "${retry_log}"
  return 1
}

install_apt_packages() {
  if ! command -v apt-get >/dev/null 2>&1; then
    return
  fi

  local missing=()
  local python_pkg="${PYTHON_BIN}"
  local python_venv_pkg="${PYTHON_BIN}-venv"
  local python_dev_pkg="${PYTHON_BIN}-dev"
  for cmd in git cmake pkg-config g++ rsync; do
    if ! command -v "${cmd}" >/dev/null 2>&1; then
      missing+=("${cmd}")
    fi
  done

  if [[ "${PYTHON_BIN}" == */* ]]; then
    python_pkg=""
    python_venv_pkg=""
    python_dev_pkg=""
  fi

  if [[ -n "${python_pkg}" ]] && ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
    missing+=("${python_pkg}")
  fi

  if [[ -n "${python_venv_pkg}" ]] && ! dpkg -s "${python_venv_pkg}" >/dev/null 2>&1; then
    missing+=("${python_venv_pkg}")
  fi

  if [[ -n "${python_dev_pkg}" ]] && ! dpkg -s "${python_dev_pkg}" >/dev/null 2>&1; then
    missing+=("${python_dev_pkg}")
  fi

  for pkg in \
    qt6-base-dev \
    qt6-base-dev-tools \
    libqt6opengl6-dev \
    libgl-dev \
    libzmq3-dev \
    python3-pip; do
    if ! dpkg -s "${pkg}" >/dev/null 2>&1; then
      missing+=("${pkg}")
    fi
  done

  if ((${#missing[@]} == 0)); then
    return
  fi

  echo "[recordlab] installing system packages: ${missing[*]}"
  if ! run_apt_update; then
    restore_disabled_apt_sources
    return 1
  fi

  local install_status=0
  if ! sudo apt-get install -y "${missing[@]}"; then
    install_status=$?
  fi

  restore_disabled_apt_sources

  if [[ "${install_status}" -ne 0 ]]; then
    return "${install_status}"
  fi
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

check_zmq_hpp() {
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

  echo "[recordlab] missing C++ ZeroMQ header zmq.hpp." >&2
  echo "[recordlab] install it with: sudo apt-get install -y libzmq3-dev" >&2
  return 1
}

check_qt6_widgets() {
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

  echo "[recordlab] Qt6 Widgets CMake package is not usable on this machine." >&2
  echo "[recordlab] sanitized env: QT_HOST_PATH='${QT_HOST_PATH:-}', QT_HOST_PATH_CMAKE_DIR='${QT_HOST_PATH_CMAKE_DIR:-}', Qt6_DIR='${Qt6_DIR:-}'" >&2
  echo "[recordlab] install/fix it with: sudo apt-get install -y qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev libgl-dev" >&2
  sed -n '1,80p' "${tmp_dir}/cmake.err" >&2 || true
  rm -rf "${tmp_dir}"
  return 1
}

validate_system_dependencies() {
  check_zmq_hpp
  check_qt6_widgets
}

ensure_python_package_source_writable() {
  local dir="$1"

  if [[ ! -d "${dir}" ]]; then
    return 0
  fi

  chmod -R u+rwX "${dir}" 2>/dev/null || true
  find "${dir}" -maxdepth 2 \( -name "*.egg-info" -o -name "*.dist-info" \) -exec rm -rf {} + 2>/dev/null || true
}

can_use_editable_install() {
  local path="$1"

  if [[ ! -d "${path}" ]] || [[ ! -w "${path}" ]]; then
    return 1
  fi

  if find "${path}" -maxdepth 2 \( -name "*.egg-info" -o -name "*.dist-info" \) | grep -q .; then
    return 1
  fi

  return 0
}

install_local_python_package() {
  local label="$1"
  local path="$2"

  ensure_python_package_source_writable "${path}"

  if can_use_editable_install "${path}"; then
    if "${RECORDLAB_PYTHON_BIN}" -m pip install -e "${path}"; then
      return 0
    fi

    echo "[recordlab] editable install failed for ${label}, retrying from a temporary copy"
  else
    echo "[recordlab] source tree for ${label} is not safe for editable install, using a temporary copy"
  fi

  local tmp_dir
  tmp_dir="$(mktemp -d)"
  rsync -a \
    --delete \
    --exclude ".git/" \
    --exclude "__pycache__/" \
    --exclude "*.pyc" \
    "${path}/" "${tmp_dir}/src/"
  "${RECORDLAB_PYTHON_BIN}" -m pip install "${tmp_dir}/src"
  rm -rf "${tmp_dir}"
}

install_apt_packages
mkdir -p "${THIRD_PARTY_ROOT}"
clone_or_update "${ECHO_REPO_URL}" "${ECHO_ROOT}"
clone_or_update "${NODES_REPO_URL}" "${NODES_ROOT}"
validate_system_dependencies

if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
  echo "[recordlab] ${PYTHON_BIN} is required. Install it or run with PYTHON_BIN=/path/to/python3.10." >&2
  exit 1
fi

if [[ -d "${VENV_DIR}" ]] && {
  [[ ! -f "${VENV_DIR}/pyvenv.cfg" ]] ||
  [[ ! -x "${VENV_DIR}/bin/python" ]] ||
  ! "${VENV_DIR}/bin/python" -m pip --version >/dev/null 2>&1
}; then
  echo "[recordlab] removing incomplete venv: ${VENV_DIR}"
  rm -rf "${VENV_DIR}"
fi

if [[ ! -d "${VENV_DIR}" ]]; then
  echo "[recordlab] creating Python 3.10 venv: ${VENV_DIR}"
  if ! "${PYTHON_BIN}" -m venv "${VENV_DIR}"; then
    echo "[recordlab] failed to create venv with ${PYTHON_BIN}." >&2
    if command -v apt-get >/dev/null 2>&1 && [[ "${PYTHON_BIN}" != */* ]]; then
      echo "[recordlab] try: sudo apt-get install -y ${PYTHON_BIN}-venv" >&2
    fi
    exit 1
  fi
fi

RECORDLAB_PYTHON_BIN="${VENV_DIR}/bin/python"
"${RECORDLAB_PYTHON_BIN}" -m pip install --upgrade pip setuptools wheel

if [[ -f "${ECHO_ROOT}/python/setup.py" ]]; then
  echo "[recordlab] installing echo_message_system Python package"
  install_local_python_package "echo_message_system" "${ECHO_ROOT}/python"
fi

if [[ -f "${NODES_ROOT}/pyproject.toml" ]]; then
  echo "[recordlab] installing Recordlab_nodes Python package"
  install_local_python_package "Recordlab_nodes" "${NODES_ROOT}"
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
bash "${HOST_ROOT}/host_scripts/build.sh"

echo "[recordlab] done. Start UI with ./RecordLabHost.sh"
