#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

find_default_uf2() {
  local matches=()
  while IFS= read -r -d '' path; do
    matches+=("$path")
  done < <(find "$script_dir" -maxdepth 1 -type f -name '*.uf2' -print0)

  case "${#matches[@]}" in
    1) printf '%s\n' "${matches[0]}" ;;
    0) return 1 ;;
    *) echo "Multiple UF2 files were found next to flash.sh. Pass the UF2 path explicitly." >&2; return 2 ;;
  esac
}

find_uf2_drive() {
  local base info
  local -a roots=(
    "/media/${USER:-}"
    "/run/media/${USER:-}"
    "/mnt"
    "/Volumes"
  )

  for base in "${roots[@]}"; do
    [[ -d "$base" ]] || continue
    while IFS= read -r -d '' info; do
      if grep -Eq 'RP2350|RPI-RP2' "$info"; then
        dirname "$info"
        return 0
      fi
    done < <(find "$base" -maxdepth 2 -type f -name 'INFO_UF2.TXT' -print0 2>/dev/null)
  done

  return 1
}

uf2_path="${1:-}"
drive_root="${2:-}"

if [[ -z "$uf2_path" ]]; then
  uf2_path="$(find_default_uf2)" || {
    echo "UF2 path is required when no package-local UF2 file is available." >&2
    exit 1
  }
fi

if [[ ! -f "$uf2_path" ]]; then
  echo "UF2 file not found: $uf2_path" >&2
  exit 1
fi

if [[ -z "$drive_root" ]]; then
  drive_root="$(find_uf2_drive)" || {
    echo "No mounted RP2350 UF2 drive was found." >&2
    exit 1
  }
fi

cp -f "$uf2_path" "$drive_root/"
sync
echo "Flashed $uf2_path to $drive_root"
