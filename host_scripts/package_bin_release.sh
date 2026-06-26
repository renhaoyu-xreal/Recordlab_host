#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${HOST_ROOT}/build"
DIST_ROOT="${HOST_ROOT}/dist"
PACKAGE_NAME="RecordLabHostBin"
PACKAGE_DIR="${DIST_ROOT}/${PACKAGE_NAME}"
ARCHIVE_PATH="${DIST_ROOT}/${PACKAGE_NAME}-linux-x86_64.tar.gz"

SKIP_BUILD=0
CLEAN_DIST=1

usage() {
  cat <<'EOF'
Usage: host_scripts/package_bin_release.sh [options]

Options:
  --skip-build   Reuse existing build/ outputs
  --keep-dist    Do not delete dist/RecordLabHostBin before packaging
  -h, --help     Show this help
EOF
}

while (($# > 0)); do
  case "$1" in
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --keep-dist)
      CLEAN_DIST=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

require_path() {
  local path="$1"
  if [[ ! -e "${path}" ]]; then
    echo "Missing required package input: ${path}" >&2
    exit 1
  fi
}

copy_tree() {
  local src_rel="$1"
  local dst_rel="$2"
  require_path "${HOST_ROOT}/${src_rel}"
  mkdir -p "$(dirname "${PACKAGE_DIR}/${dst_rel}")"
  rsync -a --delete \
    --exclude ".git/" \
    --exclude "__pycache__/" \
    --exclude "*.pyc" \
    --exclude ".pytest_cache/" \
    --exclude ".venv/" \
    --exclude ".venv-*/" \
    --exclude "build/" \
    --exclude "dist/" \
    --exclude "data/" \
    --exclude "log/" \
    --exclude "logs/" \
    --exclude "*.log" \
    --exclude "*.log.*" \
    "${HOST_ROOT}/${src_rel}" "${PACKAGE_DIR}/${dst_rel}"
}

copy_file() {
  local src_rel="$1"
  local dst_rel="$2"
  require_path "${HOST_ROOT}/${src_rel}"
  install -Dm644 "${HOST_ROOT}/${src_rel}" "${PACKAGE_DIR}/${dst_rel}"
}

copy_executable() {
  local name="$1"
  require_path "${BUILD_DIR}/${name}"
  install -Dm755 "${BUILD_DIR}/${name}" "${PACKAGE_DIR}/bin/${name}"
}

if ! command -v rsync >/dev/null 2>&1; then
  echo "rsync is required for packaging. Install it with: sudo apt-get install -y rsync" >&2
  exit 1
fi

if [[ "${SKIP_BUILD}" != "1" ]]; then
  "${HOST_ROOT}/host_scripts/build.sh"
fi

if [[ "${CLEAN_DIST}" == "1" ]]; then
  rm -rf "${PACKAGE_DIR}"
fi
mkdir -p "${PACKAGE_DIR}/bin" "${DIST_ROOT}"

copy_executable "recordlab_host_app"
copy_executable "recordlab_cli"

copy_tree "config/" "config/"
copy_tree "third_party/Recordlab_nodes/" "third_party/Recordlab_nodes/"
copy_tree "third_party/echo_message_system/" "third_party/echo_message_system/"
if [[ -d "${HOST_ROOT}/third_party/xreal_glasses" ]]; then
  copy_tree "third_party/xreal_glasses/" "third_party/xreal_glasses/"
fi
copy_tree "docs/" "docs/"
copy_file "README.md" "README.md"
copy_file "host_scripts/install_dependencies.sh" "host_scripts/install_dependencies.sh"

cat > "${PACKAGE_DIR}/bin/start_recordlab.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

APP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_ROOT="${APP_ROOT}/third_party"
ECHO_ROOT="${ECHO_MESSAGE_SYSTEM_ROOT:-${THIRD_PARTY_ROOT}/echo_message_system}"
NODES_ROOT="${RECORDLAB_NODES_ROOT:-${THIRD_PARTY_ROOT}/Recordlab_nodes}"
AGENTS_CONFIG="${RECORDLAB_AGENTS_CONFIG:-${NODES_ROOT}/config/agents_config.json}"
APP_BIN="${APP_ROOT}/bin/recordlab_host_app"
VENV_DIR="${RECORDLAB_VENV_DIR:-${APP_ROOT}/.venv-py310}"

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
    echo "[recordlab-bin] missing ${message}: ${path}" >&2
    exit 1
  fi
}

require_path "${APP_BIN}" "recordlab_host_app"
require_path "${NODES_ROOT}/recordlab_nodes/core/node_runtime.py" "Recordlab_nodes runtime"
require_path "${ECHO_MESSAGE_SYSTEM_PYTHON_ROOT}/message_system" "echo_message_system Python package"
require_path "${AGENTS_CONFIG}" "agents_config.json"

if ! "${RECORDLAB_PYTHON_BIN}" -c "import recordlab_nodes, message_system" >/dev/null 2>&1; then
  echo "[recordlab-bin] Python 依赖未就绪: ${RECORDLAB_PYTHON_BIN}" >&2
  echo "[recordlab-bin] 可先运行 ${APP_ROOT}/host_scripts/install_dependencies.sh" >&2
  exit 1
fi

echo "[recordlab-bin] cleaning old RecordLab processes"
pkill -x "recordlab_master_app" 2>/dev/null || true
pkill -x "recordlab_host_app" 2>/dev/null || true
pkill -f "[p]ython.*-m recordlab_nodes\\.core\\.node_runtime" 2>/dev/null || true

echo "[recordlab-bin] starting UI"
exec "${APP_BIN}" "${AGENTS_CONFIG}"
EOF

cat > "${PACKAGE_DIR}/RecordLabHost.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${APP_DIR}/bin/start_recordlab.sh" "$@"
EOF

cat > "${PACKAGE_DIR}/BIN_PACKAGE_README.txt" <<'EOF'
RecordLab Host bin package

Contents:
  bin/recordlab_host_app
  bin/recordlab_cli
  bin/start_recordlab.sh

Recommended first-time setup:
  ./host_scripts/install_dependencies.sh

Start UI:
  ./RecordLabHost.sh

This package keeps the runtime resources required by RecordLab under the
package root, while placing executable entry points in bin/.
EOF

find "${PACKAGE_DIR}" -type f -name "*.sh" -exec chmod +x {} \;
tar -C "${DIST_ROOT}" -czf "${ARCHIVE_PATH}" "${PACKAGE_NAME}"

echo "=================================================="
echo "RecordLab Host bin package is ready:"
echo "  Folder : ${PACKAGE_DIR}"
echo "  Archive: ${ARCHIVE_PATH}"
echo "  Entry  : ${PACKAGE_DIR}/RecordLabHost.sh"
echo "  Bin UI : ${PACKAGE_DIR}/bin/recordlab_host_app"
echo "=================================================="
