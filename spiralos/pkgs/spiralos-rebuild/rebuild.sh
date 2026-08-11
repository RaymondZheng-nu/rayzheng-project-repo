#!/usr/bin/env bash
set -euo pipefail

# wraps nixos-rebuild, shows what a switch would change before you commit
# also turns rollback into one word instead of a flag to remember

usage() {
  echo "usage: spiralos-rebuild [diff|switch|boot|test|rollback] [flake target]"
  echo "  diff      build the new system, show the closure diff, do not switch"
  echo "  switch    build, show the diff, then switch (default)"
  echo "  boot      build, show the diff, then set as boot default only"
  echo "  test      build, show the diff, then activate without setting boot default"
  echo "  rollback  boot into the previous system generation"
}

cmd="${1:-switch}"
target="${2:-.#live}"

# compares the running system to the one we just built
show_diff() {
  local new_path="$1"
  local current_path
  current_path=$(readlink -f /run/current-system)
  if [ "$current_path" = "$new_path" ]; then
    echo "no changes, system is already up to date"
    return
  fi
  echo "--- closure diff, current -> new ---"
  nix store diff-closures "$current_path" "$new_path" || true
}

case "$cmd" in
  diff)
    new_path=$(nix build --no-link --print-out-paths \
      "${target}.config.system.build.toplevel")
    show_diff "$new_path"
    ;;
  switch|boot|test)
    new_path=$(nix build --no-link --print-out-paths \
      "${target}.config.system.build.toplevel")
    show_diff "$new_path"
    sudo nixos-rebuild "$cmd" --flake "$target"
    ;;
  rollback)
    sudo nixos-rebuild switch --rollback
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    usage
    exit 1
    ;;
esac
