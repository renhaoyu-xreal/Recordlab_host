#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_DIR="${HOST_ROOT}/dist/RecordLabHostBin"
REMOTE_HOST="${RECORDLAB_RELEASE_HOST:-nreal@10.2.11.200}"
REMOTE_ROOT="${RECORDLAB_RELEASE_ROOT:-/home/nreal/nviz_record_data/Recordlab_host}"
REMOTE_PASSWORD="${RECORDLAB_RELEASE_PASSWORD:-1}"
LOCK_PATH="${REMOTE_ROOT}/.publish_in_progress"
SKIP_PACKAGE=0

usage() {
  cat <<'EOF'
Usage: host_scripts/deploy_bin_release.sh [options]

Options:
  --skip-package  Reuse existing dist/RecordLabHostBin
  -h, --help      Show this help
EOF
}

while (($# > 0)); do
  case "$1" in
    --skip-package)
      SKIP_PACKAGE=1
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

require_cmd() {
  local name="$1"
  if ! command -v "${name}" >/dev/null 2>&1; then
    echo "${name} is required for deploy. Install it first." >&2
    exit 1
  fi
}

remote_ssh() {
  sshpass -p "${REMOTE_PASSWORD}" ssh "${REMOTE_HOST}" "$@"
}

cleanup() {
  remote_ssh "rm -f '${LOCK_PATH}'" >/dev/null 2>&1 || true
}
trap cleanup EXIT

require_cmd sshpass
require_cmd rsync
require_cmd ssh

if [[ "${SKIP_PACKAGE}" != "1" ]]; then
  "${HOST_ROOT}/host_scripts/package_bin_release.sh"
fi

if [[ ! -d "${PACKAGE_DIR}" ]]; then
  echo "Missing package directory: ${PACKAGE_DIR}" >&2
  exit 1
fi

echo "[deploy] ensuring remote release directory exists"
remote_ssh "mkdir -p '${REMOTE_ROOT}' && : > '${LOCK_PATH}'"

echo "[deploy] syncing ${PACKAGE_DIR} to ${REMOTE_HOST}:${REMOTE_ROOT}"
sshpass -p "${REMOTE_PASSWORD}" rsync -a --delete \
  --exclude ".publish_in_progress" \
  "${PACKAGE_DIR}/" "${REMOTE_HOST}:${REMOTE_ROOT}/"

echo "[deploy] release deployed successfully"
