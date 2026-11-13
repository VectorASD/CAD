#!/bin/bash
set -eu

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

SRC="$SCRIPT_DIR/src"
INCLUDE="$SCRIPT_DIR/include"
OBJ="$SCRIPT_DIR/obj"
PYTHON_DIR="$SCRIPT_DIR/python"

CHECK_TARGETS=("$SRC" "$INCLUDE" "$PYTHON_DIR" "$0")

# Check for non-ASCII characters
if grep -rnP '[^\x00-\x7F]' "${CHECK_TARGETS[@]}" >/dev/null 2>&1; then
    echo "Non-ASCII characters found:" >&2
    grep -rnP '[^\x00-\x7F]' "${CHECK_TARGETS[@]}" >&2
    exit 1
fi

# Check for trailing whitespaces (spaces or tabs at the end of lines)
if grep -rnP '[ \t]+$' "${CHECK_TARGETS[@]}" >/dev/null 2>&1; then
    echo "Trailing whitespaces found:" >&2
    grep -rnP '[ \t]+$' "${CHECK_TARGETS[@]}" >&2
    exit 1
fi

# --- Compiler ---

# Find the best clang++ available:
#   1. prefer plain "clang++" if it exists;
#   2. otherwise pick the highest-numbered "clang++-N" (e.g. clang++-23).
find_clangxx() {
    if command -v clang++ >/dev/null 2>&1; then
        echo "clang++"
        return 0
    fi

    local best="" best_num=-1 cand num
    # Only consider versioned binaries on PATH, highest N first.
    while IFS= read -r cand; do
        num="${cand##*clang++-}"      # strip prefix, keep version
        case "$num" in
            ''|*[!0-9]*) continue ;;  # skip non-numeric suffixes
        esac
        if [ "$num" -gt "$best_num" ]; then
            best_num=$num
            best=$cand
        fi
    done < <(compgen -c clang++ 2>/dev/null | grep -E '^clang\+\+-[0-9]+$' | sort -u)

    if [ -n "$best" ]; then
        echo "$best"
        return 0
    fi
    return 1
}

# Returns the command to install ccache for the current system.
# Returns 1 if no known package manager is detected.
detect_ccache_install_cmd() {
    # Termux: $PREFIX = /data/data/com.termux/files/usr
    if [ -n "${PREFIX:-}" ] && [[ "$PREFIX" == *"com.termux"* ]]; then
        echo "pkg install ccache"
        return 0
    fi

    if   command -v apt          >/dev/null 2>&1; then echo "sudo apt install ccache"
    elif command -v dnf          >/dev/null 2>&1; then echo "sudo dnf install ccache"
    elif command -v pacman       >/dev/null 2>&1; then echo "sudo pacman -S ccache"
    elif command -v zypper       >/dev/null 2>&1; then echo "sudo zypper install ccache"
    elif command -v xbps-install >/dev/null 2>&1; then echo "sudo xbps-install ccache"
    elif command -v brew         >/dev/null 2>&1; then echo "brew install ccache"
    elif command -v apk          >/dev/null 2>&1; then echo "sudo apk add ccache"
    elif command -v pkg          >/dev/null 2>&1; then echo "sudo pkg install ccache"
    else
        return 1
    fi
}

if ! CXX_BIN=$(find_clangxx); then
    echo "No clang++ found on PATH (tried 'clang++' and 'clang++-N')." >&2
    echo "Install LLVM, e.g.: pkg install llvm" >&2
    exit 1
fi

C_BIN="${CXX_BIN//++/}"  # clang++-23 -> clang-23
if ! command -v "$C_BIN" >/dev/null 2>&1; then
    C_BIN="cc"
fi

# Prefer ccache if available; otherwise, tell the user how to install it.
if command -v ccache >/dev/null 2>&1; then
    CC=("ccache" "$CXX_BIN")
    CC_C=("ccache" "$C_BIN")
else
    CC=("$CXX_BIN")
    CC_C=("$C_BIN")
    echo "ccache is not installed. Compilation will proceed without it." >&2

    if install_cmd=$(detect_ccache_install_cmd); then
        echo "To install: $install_cmd" >&2
    else
        echo "No known package manager found. Install ccache manually." >&2
    fi
fi
echo "Using compiler: ${CC[*]}"

# --- Flags ---

CFLAGS=(
    "-I$INCLUDE"
    "-std=c++20"
    "-fPIC"
    "-MMD"
    "-O3"
    "-g"
    "-DNDEBUG"
    "-Wall" "-Wextra"
)

# No external libraries yet (no GMP, no JVM).
LDFLAGS=()

# --- Helpers ---

mkdir -p "$OBJ"

# Compile a single translation unit.
# Usage: compile <path under $SRC, no .cpp>
compile() {
    local name="$1"
    mkdir -p "$(dirname "$OBJ/$name")"
    echo "  CXX  $name.cpp"
    "${CC[@]}" "${CFLAGS[@]}" \
       -MF "$OBJ/$name.d" \
        -o "$OBJ/$name.o" \
        -c "$SRC/$name.cpp"
}

# --- Build: geom_tests ---

compile main
compile tests

echo "  LD   geom_tests"
"${CC[@]}" -o "$SCRIPT_DIR/geom_tests" \
    "$OBJ/main.o" "$OBJ/tests.o"

echo "  STRIP geom_tests"
strip "$SCRIPT_DIR/geom_tests" -o "$SCRIPT_DIR/geom_tests.stripped"
mv "$SCRIPT_DIR/geom_tests.stripped" "$SCRIPT_DIR/geom_tests"

echo "  RUN  geom_tests"
"$SCRIPT_DIR/geom_tests"
echo ""

# --- Build: graphics_server ---

compile server
compile render/render
compile render/stream

echo "  CC   render/glad.c"
"${CC_C[@]}" -I"$INCLUDE" -O3 -g -Wall \
    -c "$SRC/render/glad.c" -o "$OBJ/render/glad.o"

echo "  LD   graphics_server"
"${CC[@]}" -o "$SCRIPT_DIR/graphics_server" \
    "$OBJ/server.o" \
    "$OBJ/render/render.o" \
    "$OBJ/render/stream.o" \
    "$OBJ/render/glad.o" \
    -lOSMesa -lavcodec -lavutil -lswscale -lpthread

echo "  STRIP graphics_server"
strip "$SCRIPT_DIR/graphics_server" -o "$SCRIPT_DIR/graphics_server.stripped"
mv "$SCRIPT_DIR/graphics_server.stripped" "$SCRIPT_DIR/graphics_server"
echo "  DONE graphics_server built successfully."

# --- Deploy python/ to Windows Downloads (WSL convenience) ---

DOWNLOAD_DIR="/mnt/c/Users/VectorASD/Downloads"
if [ -d "$DOWNLOAD_DIR" ]; then
    echo "  CP   python/ -> $DOWNLOAD_DIR"
    rm -rf "$DOWNLOAD_DIR/python"
    cp -r "$PYTHON_DIR" "$DOWNLOAD_DIR/"
fi
