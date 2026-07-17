#!/bin/bash
# Compile all Vulkan GLSL shaders to SPIR-V, in place (<name>.spv next to source).
# GL variants (*_gl.*) are runtime-compiled and skipped.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SHADER_DIRS=(
  "$ROOT_DIR/Engine/Engine/assets/shaders"
  "$ROOT_DIR/Game/assets/shaders"
)

if ! command -v glslc >/dev/null; then
  echo "error: glslc not found" >&2
  exit 1
fi

fail=0
count=0
for dir in "${SHADER_DIRS[@]}"; do
  if [[ ! -d "$dir" ]]; then
    echo "error: shader dir missing: $dir" >&2
    fail=1
    continue
  fi
  for shader in "$dir"/*.vert "$dir"/*.frag "$dir"/*.comp; do
    [[ -f "$shader" ]] || continue
    case "$(basename "$shader")" in *_gl.*) continue ;; esac
    if glslc "$shader" -o "$shader.spv"; then
      echo "compiled ${shader#$ROOT_DIR/} -> ${shader#$ROOT_DIR/}.spv"
      count=$((count + 1))
    else
      echo "FAILED: $shader" >&2
      fail=1
    fi
  done
done

echo "$count shaders compiled"
[[ $count -gt 0 ]] || fail=1
exit $fail
