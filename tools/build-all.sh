#!/usr/bin/env bash
#
# Compile every sketch in this repo, or just the ones named.
#
# This is the same command CI runs — CI calls this script, one sketch per
# job — so a green run here means a green run there. Use it before pushing.
#
#   tools/build-all.sh                     compile everything
#   tools/build-all.sh 01_blink            compile one
#   tools/build-all.sh --strict            treat warnings as failures
#   tools/build-all.sh --clean             force a full rebuild
#
# Every sketch in this repo compiles with zero warnings, and that is worth
# keeping true, so CI runs with --strict.
#
# A caveat about warnings: arduino-cli caches builds, and a cached build
# does not re-run the compiler, so it reports no warnings even when the
# code has some. CI is unaffected because its sketch cache is always cold,
# but locally, a run straight after a successful one can look cleaner than
# it is. Use --clean when you actually want to trust the warning count.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCHES_DIR="$REPO_ROOT/sketches"
PROFILE="atomlite"
STRICT=0
CLEAN=0

if [ -t 1 ]; then
  BOLD=$'\033[1m'; RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; OFF=$'\033[0m'
else
  BOLD=""; RED=""; GREEN=""; YELLOW=""; OFF=""
fi

usage() {
  sed -n '3,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

SKETCHES=()
while [ $# -gt 0 ]; do
  case "$1" in
    --strict)     STRICT=1 ;;
    --clean)      CLEAN=1 ;;
    -h|--help)    usage 0 ;;
    -*)           echo "Unknown option: $1" >&2; usage 1 ;;
    *)            SKETCHES+=("$1") ;;
  esac
  shift
done

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "${RED}arduino-cli is not installed.${OFF}" >&2
  echo "  brew install arduino-cli        (see docs/mac-setup.md)" >&2
  exit 127
fi

# No sketches named: do all of them, in directory order, which is also the
# order they are meant to be worked through.
if [ ${#SKETCHES[@]} -eq 0 ]; then
  for dir in "$SKETCHES_DIR"/*/; do
    [ -d "$dir" ] || continue
    SKETCHES+=("$(basename "$dir")")
  done
fi

if [ ${#SKETCHES[@]} -eq 0 ]; then
  echo "${RED}No sketches found under $SKETCHES_DIR${OFF}" >&2
  exit 1
fi

LOG_DIR="$(mktemp -d)"
trap 'rm -rf "$LOG_DIR"' EXIT

failed=0
warned=0
results=()

for name in "${SKETCHES[@]}"; do
  dir="$SKETCHES_DIR/$name"

  if [ ! -f "$dir/$name.ino" ]; then
    # Arduino requires the sketch folder and its .ino to share a name, so
    # this is a real error rather than a naming preference.
    echo "${RED}$name: expected $dir/$name.ino${OFF}" >&2
    results+=("$name|MISSING|-|-")
    failed=1
    continue
  fi

  printf '%s… ' "$name"
  log="$LOG_DIR/$name.log"

  clean_flag=()
  [ "$CLEAN" -eq 1 ] && clean_flag=(--clean)

  if (cd "$dir" && arduino-cli compile \
        --profile "$PROFILE" \
        --warnings all \
        --output-dir build \
        --no-color \
        "${clean_flag[@]+"${clean_flag[@]}"}") > "$log" 2>&1; then

    warnings=$(grep -c 'warning:' "$log" || true)
    size=$(grep -o 'Sketch uses [0-9]* bytes' "$log" | head -1 | grep -o '[0-9]*' || echo "?")

    if [ "$warnings" -gt 0 ]; then
      warned=1
      echo "${YELLOW}ok, $warnings warning(s)${OFF}"
      grep 'warning:' "$log" | sed 's/^/    /'
      results+=("$name|WARN|$size|$warnings")
      [ "$STRICT" -eq 1 ] && failed=1
    else
      echo "${GREEN}ok${OFF}"
      results+=("$name|OK|$size|0")
    fi
  else
    echo "${RED}FAILED${OFF}"
    # Only the compiler's complaints, not the toolchain download chatter.
    grep -E 'error:|Error during build' "$log" | sed 's/^/    /' || tail -20 "$log" | sed 's/^/    /'
    results+=("$name|FAILED|-|-")
    failed=1
  fi
done

echo
printf '%s%-24s %-8s %10s %9s%s\n' "$BOLD" "sketch" "result" "bytes" "warnings" "$OFF"
for row in "${results[@]}"; do
  IFS='|' read -r name status size warnings <<< "$row"
  printf '%-24s %-8s %10s %9s\n' "$name" "$status" "$size" "$warnings"
done
echo

if [ "$failed" -ne 0 ]; then
  echo "${RED}Build failed.${OFF}"
  exit 1
fi
if [ "$warned" -ne 0 ]; then
  echo "${YELLOW}Built, but with warnings. CI runs --strict and will fail on these.${OFF}"
  exit 0
fi
echo "${GREEN}All good.${OFF}"
