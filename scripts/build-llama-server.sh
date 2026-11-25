#!/usr/bin/env bash
set -euo pipefail

readonly llama_commit="2d8a32502909ca065c41c4cf5f2fb1c427c047fc"
source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${PLATYPUS_BUILD_DIR:-${source_root}/build/install}"
prefix="${PLATYPUS_INSTALL_PREFIX:-${HOME}/.local}"
llama_source="${build_root}/_deps/llama.cpp"
llama_build="${build_root}/llama-server"
backend="${PLATYPUS_LLAMA_BACKEND:-auto}"
build_jobs="${PLATYPUS_BUILD_JOBS:-2}"
backend_reason="explicit PLATYPUS_LLAMA_BACKEND setting"

if [[ "${backend}" == auto ]]; then
  case "$(uname -s)" in
    Darwin)
      backend=metal
      backend_reason="native Apple Metal support"
      ;;
    Linux)
      if command -v nvcc >/dev/null 2>&1 && command -v nvidia-smi >/dev/null 2>&1 &&
        nvidia-smi -L >/dev/null 2>&1; then
        backend=cuda
        backend_reason="CUDA toolkit and an available NVIDIA GPU"
      elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan &&
        command -v glslc >/dev/null 2>&1 && compgen -G '/dev/dri/renderD*' >/dev/null; then
        backend=vulkan
        backend_reason="Vulkan development tools and a Linux render device"
      else
        backend=cpu
        backend_reason="no usable CUDA or Vulkan build environment was detected"
      fi
      ;;
    *)
      backend=cpu
      backend_reason="no GPU backend auto-detection for this operating system"
      ;;
  esac
  echo "Automatically selected llama-server backend: ${backend} (${backend_reason})." >&2
fi

for command in cmake git ninja; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    echo "Missing build dependency: ${command}" >&2
    exit 1
  fi
done

case "${backend}" in
  cpu) backend_args=() ;;
  cuda) backend_args=(-DGGML_CUDA=ON -DGGML_NATIVE=OFF) ;;
  vulkan) backend_args=(-DGGML_VULKAN=ON) ;;
  metal)
    if [[ "$(uname -s)" != Darwin ]]; then
      echo "The Metal llama-server backend is available only on macOS." >&2
      exit 2
    fi
    backend_args=(-DGGML_METAL=ON)
    ;;
  *) echo "PLATYPUS_LLAMA_BACKEND must be auto, cpu, cuda, vulkan, or metal." >&2; exit 2 ;;
esac
if [[ ! "${build_jobs}" =~ ^[1-9][0-9]*$ ]]; then
  echo "PLATYPUS_BUILD_JOBS must be a positive integer." >&2
  exit 2
fi

mkdir -p "$(dirname "${llama_source}")"
if [[ ! -d "${llama_source}/.git" ]]; then
  git clone --filter=blob:none --no-checkout https://github.com/ggml-org/llama.cpp.git "${llama_source}"
fi
if [[ "$(git -C "${llama_source}" remote get-url origin)" != "https://github.com/ggml-org/llama.cpp.git" ]]; then
  echo "Unexpected llama.cpp checkout at ${llama_source}" >&2
  exit 1
fi
if [[ "$(git -C "${llama_source}" rev-parse HEAD 2>/dev/null || true)" != "${llama_commit}" ]]; then
  if [[ -n "$(git -C "${llama_source}" status --porcelain)" ]]; then
    echo "Refusing to change a modified llama.cpp checkout at ${llama_source}" >&2
    exit 1
  fi
  git -C "${llama_source}" fetch --depth=1 origin "${llama_commit}"
  git -C "${llama_source}" checkout --detach "${llama_commit}"
fi

cmake -S "${llama_source}" -B "${llama_build}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DBUILD_SHARED_LIBS=OFF \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_TOOLS=ON \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_APP=OFF \
  -DLLAMA_BUILD_SERVER=ON \
  -DLLAMA_BUILD_UI=OFF \
  -DLLAMA_USE_PREBUILT_UI=OFF \
  -DLLAMA_TOOLS_INSTALL=OFF \
  "${backend_args[@]}"
cmake --build "${llama_build}" --target llama-server --parallel "${build_jobs}"
cmake --install "${llama_build}"
echo "Installed pinned llama-server (${llama_commit}) to ${prefix}/bin/llama-server"
