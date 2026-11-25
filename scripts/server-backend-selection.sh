#!/usr/bin/env bash

# Select a backend from host facts and available build/runtime prerequisites.
# Arguments: OS, CUDA toolkit, NVIDIA device, Vulkan development tools, DRM render device.
platypus_select_server_backend() {
  local operating_system="$1"
  local cuda_toolkit="$2"
  local nvidia_device="$3"
  local vulkan_tools="$4"
  local render_device="$5"

  case "${operating_system}" in
    Darwin) printf 'metal\n' ;;
    Linux)
      if [[ "${cuda_toolkit}" == yes && "${nvidia_device}" == yes ]]; then
        printf 'cuda\n'
      elif [[ "${vulkan_tools}" == yes && "${render_device}" == yes ]]; then
        printf 'vulkan\n'
      else
        printf 'cpu\n'
      fi
      ;;
    *) printf 'cpu\n' ;;
  esac
}
