#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
  echo "Platypus v1.0 supports Linux x86_64 with this installer." >&2
  exit 2
fi

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${PLATYPUS_INSTALL_PREFIX:-${HOME}/.local}"
build_dir="${PLATYPUS_BUILD_DIR:-${source_dir}/build/install}"

for command in cmake ninja c++ git; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    echo "Missing build dependency: ${command}" >&2
    exit 1
  fi
done

cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${prefix}"
cmake --build "${build_dir}" --parallel
cmake --install "${build_dir}"
"${source_dir}/scripts/build-llama-server.sh"
echo "Installed Platypus to ${prefix}/bin/platypus"
