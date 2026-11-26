#!/usr/bin/env bash
set -euo pipefail

readonly llama_commit="2d8a32502909ca065c41c4cf5f2fb1c427c047fc"
source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${source_root}/scripts/server-backend-selection.sh"
build_root="${PLATYPUS_BUILD_DIR:-${source_root}/build/install}"
prefix="${PLATYPUS_INSTALL_PREFIX:-${HOME}/.local}"
llama_source="${build_root}/_deps/llama.cpp"
llama_build="${build_root}/llama-server"
backend="${PLATYPUS_LLAMA_BACKEND:-auto}"
build_jobs="${PLATYPUS_BUILD_JOBS:-2}"
backend_reason="explicit PLATYPUS_LLAMA_BACKEND setting"

if [[ "${backend}" == auto ]]; then
  cuda_toolkit=no
  nvidia_device=no
  vulkan_tools=no
  render_device=no
  if command -v nvcc >/dev/null 2>&1; then
    cuda_toolkit=yes
  fi
  if command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi -L >/dev/null 2>&1; then
    nvidia_device=yes
  fi
  if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan &&
    command -v glslc >/dev/null 2>&1; then
    vulkan_tools=yes
  fi
  if compgen -G '/dev/dri/renderD*' >/dev/null; then
    render_device=yes
  fi
  backend="$(platypus_select_server_backend "$(uname -s)" "${cuda_toolkit}" \
    "${nvidia_device}" "${vulkan_tools}" "${render_device}")"
  case "${backend}" in
    metal) backend_reason="native Apple Metal support" ;;
    cuda) backend_reason="CUDA toolkit and an available NVIDIA GPU" ;;
    vulkan) backend_reason="Vulkan development tools and a Linux render device" ;;
    cpu) backend_reason="no usable CUDA or Vulkan build environment was detected" ;;
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
  cpu)
    backend_args=()
    if [[ "$(uname -s)" == Darwin ]]; then
      backend_args=(-DGGML_METAL=OFF)
    fi
    ;;
  cuda)
    if [[ "$(uname -s)" != Linux ]]; then
      echo "The CUDA llama-server backend is supported only on Linux." >&2
      exit 2
    fi
    backend_args=(-DGGML_CUDA=ON -DGGML_NATIVE=OFF)
    ;;
  vulkan)
    if [[ "$(uname -s)" != Linux ]]; then
      echo "The Vulkan llama-server backend is supported only on Linux." >&2
      exit 2
    fi
    backend_args=(-DGGML_VULKAN=ON)
    ;;
  metal)
    if [[ "$(uname -s)" != Darwin ]]; then
      echo "The Metal llama-server backend is available only on macOS." >&2
      exit 2
    fi
    # The pinned Metal source uses std::unique_ptr without directly including <memory>.
    metal_cxx_flags="${CXXFLAGS:-} -include memory"
    backend_args=(-DGGML_METAL=ON "-DCMAKE_CXX_FLAGS=${metal_cxx_flags}")
    ;;
  *) echo "PLATYPUS_LLAMA_BACKEND must be auto, cpu, cuda, vulkan, or metal." >&2; exit 2 ;;
esac
if [[ ! "${build_jobs}" =~ ^[1-9][0-9]*$ ]]; then
  echo "PLATYPUS_BUILD_JOBS must be a positive integer." >&2
  exit 2
fi

mkdir -p "$(dirname "${llama_source}")"
if [[ ! -d "${llama_source}/.git" ]]; then
  git clone --filter=blob:none --depth=1 https://github.com/ggml-org/llama.cpp.git "${llama_source}"
fi
if [[ "$(git -C "${llama_source}" remote get-url origin)" != "https://github.com/ggml-org/llama.cpp.git" ]]; then
  echo "Unexpected llama.cpp checkout at ${llama_source}" >&2
  exit 1
fi
if [[ -n "$(git -C "${llama_source}" status --porcelain)" ]]; then
  echo "Refusing to build a modified llama.cpp checkout at ${llama_source}" >&2
  exit 1
fi
if [[ "$(git -C "${llama_source}" rev-parse HEAD 2>/dev/null || true)" != "${llama_commit}" ]]; then
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
release_target="${PLATYPUS_RELEASE_TARGET:-}"
cuda_version=""
if [[ -z "${release_target}" ]]; then
  case "$(uname -s):${backend}" in
    Darwin:metal) release_target="macos-arm64-metal" ;;
    Darwin:cpu) release_target="macos-arm64-cpu" ;;
    Linux:cpu) release_target="linux-x86_64-cpu" ;;
    Linux:vulkan) release_target="linux-x86_64-vulkan" ;;
    Linux:cuda)
      cuda_version="$(nvcc --version | sed -n 's/.*release \([0-9][0-9]*\)\..*/\1/p' | tail -n 1)"
      case "${cuda_version}" in
        12) release_target="linux-x86_64-cuda12" ;;
        13) release_target="linux-x86_64-cuda13" ;;
        *) echo "CUDA release target must use toolkit 12 or 13." >&2; exit 2 ;;
      esac
      ;;
    *) echo "Cannot determine a supported release target." >&2; exit 2 ;;
  esac
fi
case "${release_target}" in
  linux-x86_64-cpu|linux-x86_64-vulkan|linux-x86_64-cuda12|linux-x86_64-cuda13|macos-arm64-cpu|macos-arm64-metal) ;;
  *) echo "Unsupported PLATYPUS_RELEASE_TARGET: ${release_target}" >&2; exit 2 ;;
esac
case "${release_target}:${backend}" in
  linux-x86_64-cpu:cpu|linux-x86_64-vulkan:vulkan|macos-arm64-cpu:cpu|macos-arm64-metal:metal) ;;
  linux-x86_64-cuda12:cuda)
    [[ -n "${cuda_version}" ]] || cuda_version="$(nvcc --version | sed -n 's/.*release \([0-9][0-9]*\)\..*/\1/p' | tail -n 1)"
    [[ "${cuda_version}" == 12 ]] || { echo "CUDA 12 target requires CUDA 12." >&2; exit 2; }
    ;;
  linux-x86_64-cuda13:cuda)
    [[ -n "${cuda_version}" ]] || cuda_version="$(nvcc --version | sed -n 's/.*release \([0-9][0-9]*\)\..*/\1/p' | tail -n 1)"
    [[ "${cuda_version}" == 13 ]] || { echo "CUDA 13 target requires CUDA 13." >&2; exit 2; }
    ;;
  *) echo "Release target does not match the selected OS/backend." >&2; exit 2 ;;
esac
mkdir -p "${prefix}/share/platypus"
printf '%s\n' "${release_target}" > "${prefix}/share/platypus/install-target"
echo "Installed pinned llama-server (${llama_commit}) to ${prefix}/bin/llama-server"
