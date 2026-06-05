#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${HOST_ROOT}/build"
DIST_ROOT="${HOST_ROOT}/dist"
PACKAGE_NAME="RecordLabHost"
PACKAGE_DIR="${DIST_ROOT}/${PACKAGE_NAME}"
ARCHIVE_PATH="${DIST_ROOT}/${PACKAGE_NAME}-linux-x86_64.tar.gz"

SKIP_BUILD=0
CLEAN_DIST=1

usage() {
  cat <<'EOF'
Usage: scripts/package_release.sh [options]

Options:
  --skip-build   Reuse existing build/ outputs
  --keep-dist    Do not delete dist/RecordLabHost before packaging
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
    --exclude "build/" \
    --exclude "dist/" \
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
  install -Dm755 "${BUILD_DIR}/${name}" "${PACKAGE_DIR}/build/${name}"
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
mkdir -p "${PACKAGE_DIR}/build" "${DIST_ROOT}"

copy_executable "recordlab_host_app"
copy_executable "recordlab_cli"

copy_tree "config/" "config/"
copy_tree "host_scripts/" "host_scripts/"
copy_tree "third_party/Recordlab_nodes/" "third_party/Recordlab_nodes/"
copy_tree "third_party/echo_message_system/" "third_party/echo_message_system/"
if [[ -d "${HOST_ROOT}/third_party/xreal_glasses" ]]; then
  copy_tree "third_party/xreal_glasses/" "third_party/xreal_glasses/"
fi
copy_tree "docs/" "docs/"
copy_tree "scripts/" "scripts/"
copy_file "README.md" "README.md"

cat > "${PACKAGE_DIR}/RecordLabHost.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${APP_DIR}/host_scripts/start_recordlab.sh" "$@"
EOF

cat > "${PACKAGE_DIR}/PACKAGE_README.txt" <<'EOF'
RecordLab Host packaged build

First-time setup:
  ./host_scripts/install_dependencies.sh

Start UI:
  ./RecordLabHost.sh

The package contains Host, Recordlab_nodes, echo_message_system and runtime
configuration. Runtime paths are resolved from the package root.
EOF

find "${PACKAGE_DIR}" -type f -name "*.sh" -exec chmod +x {} \;
tar -C "${DIST_ROOT}" -czf "${ARCHIVE_PATH}" "${PACKAGE_NAME}"

echo "=================================================="
echo "RecordLab Host package is ready:"
echo "  Folder : ${PACKAGE_DIR}"
echo "  Archive: ${ARCHIVE_PATH}"
echo "  Entry  : ${PACKAGE_DIR}/RecordLabHost.sh"
echo "=================================================="
