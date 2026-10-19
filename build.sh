#!/bin/bash
set -eu
cd "$(dirname "$0")"

# --- Host Platform -----------------------------------------------------------
host_os="$(uname -s)"
host_arch="$(uname -m)"
cc_arch=""
if [[ "$host_arch" == "x86_64" ]]; then cc_arch="-msse2 -mcx16"; fi
cc_platform=""
if [[ "$host_os" == "Darwin" ]]; then cc_platform="-mmacosx-version-min=13.0"; fi
if [[ "$host_os" != "Darwin" && "$host_os" != "Linux" ]]; then
  echo "Unsupported host operating system: $host_os" >&2
  exit 1
fi
if [[ "$host_os" == "Darwin" && "$host_arch" != "arm64" ]]; then
  echo "The macOS debugger requires a native ARM64 shell (arch -arm64 ./build.sh)." >&2
  exit 1
fi

# --- Unpack Arguments --------------------------------------------------------
auto_compile_flags=""
for arg in "$@"; do declare $arg='1'; done
if [[ "$#" == "0" ]]; then raddbg='1'; fi
if [[ "$#" == "1" && "${release:-0}" == "1" ]]; then raddbg='1'; fi
if [[ "${asan:-0}" == "1" ]]; then
  echo "[asan enabled]"
  auto_compile_flags="$auto_compile_flags -fsanitize=address"
fi

# --- Mix Git Commit ID -------------------------------------------------------
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  auto_compile_flags="$auto_compile_flags -DBUILD_GIT_HASH=\"$(git describe --always --dirty)\" -DBUILD_GIT_HASH_FULL=$(git rev-parse HEAD)"
fi

# --- Compile/Link Line Definitions -------------------------------------------
cc_cflags_gcc=""
cc_cflags_clang=" -fdiagnostics-absolute-paths -Wno-for-loop-analysis -Wno-incompatible-pointer-types-discards-qualifiers -Wno-initializer-overrides -Wno-compare-distinct-pointer-types -Wno-single-bit-bitfield-constant-conversion -Wno-deprecated-declarations -Wno-writable-strings -Wno-unknown-warning-option -Wno-deprecated-register -Wno-unused-local-typedef"
cc_common="${auto_compile_flags} $cc_arch $cc_platform -I../src/ -I../local/ -D_GNU_SOURCE -g -Wall -Wno-missing-braces -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-value -D_USE_MATH_DEFINES -Dgnu_printf=printf"
cc_debug="-g -O0 -DBUILD_DEBUG=1 ${cc_common}"
cc_release="-g -O2 -DBUILD_DEBUG=0 ${cc_common}"
cc_link="-lpthread -lm -lrt -ldl"
cc_graphical_language=""
if [[ "$host_os" == "Darwin" ]]; then
  cc_link="-lpthread -lm"
  cc_graphical_language="-x objective-c"
fi

# --- Per-Build Settings ------------------------------------------------------
cc_link_dll="-fPIC"

# --- External Libraries ------------------------------------------------------
# sudo apt install -y pkg-config libfreetype6-dev libx11-dev libxext-dev libxfixes-dev libxrandr-dev libgl-dev libegl-dev
if [[ "$host_os" == "Darwin" ]]; then
  cc_font_provider="-framework CoreText -framework CoreGraphics"
  cc_os_gfx="-framework Cocoa -framework CoreServices"
  cc_render="-framework OpenGL"
elif command -v pkg-config >/dev/null 2>&1; then
  cc_font_provider="$(pkg-config --cflags --libs freetype2)"
  cc_os_gfx="$(pkg-config --cflags --libs x11 xext xfixes xrandr)"
  cc_render="$(pkg-config --cflags --libs gl egl)"
else
  cc_font_provider="-I/usr/include/freetype2 -lfreetype"
  cc_os_gfx="-lX11 -lXext -lXfixes -lXrandr"
  cc_render="-lGL -lEGL"
fi

# --- Icons -------------------------------------------------------------------
cc_icon=""
if [[ "$host_os" == "Linux" ]]; then cc_icon="-DLNX_WM_ICON=1"; fi

# --- Choose Compile/Link Lines -----------------------------------------------
if   [[ "${gcc:-0}"   == "1" ]]; then compiler="${CC:-gcc}   $cc_cflags_gcc";   ar="${AR:-ar}";      echo "[gcc compile]";
elif [[ "${clang:-1}" == "1" ]]; then compiler="${CC:-clang} $cc_cflags_clang"; ar="${AR:-ar}"; echo "[clang compile]";
fi
if   [[ "${release:-0}" == "1" ]]; then echo "[release mode]"; compile="$compiler $cc_release";
elif [[ "${debug:-1}"   == "1" ]]; then echo "[debug mode]";   compile="$compiler $cc_debug";
fi

oodle_flags=()
blake3_lib="blake3.a"
if [[ "$host_arch" == "arm64" || "$host_arch" == "aarch64" ]]; then blake3_lib="blake3_arm64.a"; fi
compile() {
  if [[ "$host_os" == "Darwin" ]]; then
    $compile ${oodle_flags[@]+"${oodle_flags[@]}"} "$@" -x none "$blake3_lib"
  else
    $compile ${oodle_flags[@]+"${oodle_flags[@]}"} -Wl,--start-group "$@" "$blake3_lib" -latomic -Wl,--end-group
  fi
}

# --- Prep Directories --------------------------------------------------------
mkdir -p build local

# --- Build & Run Metaprogram -------------------------------------------------
cd build
if [[ ! -f metagen ]]; then meta='1'; fi
if [[ "${meta:-0}" == "1" ]]; then
  echo "[building metagen]"
  $compiler $cc_debug ../src/metagen/metagen_main.c $cc_link -o metagen
fi
if [[ "${no_meta:-0}" != "1" ]]; then
  ./metagen
fi
cd ..

# --- Assemble BLAKE3 ---------------------------------------------------------
if [[ "$host_arch" == "arm64" || "$host_arch" == "aarch64" ]]; then
  if [[ ! -f "build/$blake3_lib" || src/third_party/blake3/blake3_neon.c -nt "build/$blake3_lib" ]]; then
    echo "[compiling BLAKE3 NEON]"
    $compiler $cc_platform -c -O2 -o build/blake3_neon.o src/third_party/blake3/blake3_neon.c
    $ar rcs "build/$blake3_lib" build/blake3_neon.o
  fi
elif [[ ! -f "build/blake3.a" ]]; then
  echo "[assembling blake3]"
  $compiler -c -g -o build/blake3_sse2_x86-64_unix.o   src/third_party/blake3/blake3_sse2_x86-64_unix.S
  $compiler -c -g -o build/blake3_sse41_x86-64_unix.o  src/third_party/blake3/blake3_sse41_x86-64_unix.S
  $compiler -c -g -o build/blake3_avx2_x86-64_unix.o   src/third_party/blake3/blake3_avx2_x86-64_unix.S
  $compiler -c -g -o build/blake3_avx512_x86-64_unix.o src/third_party/blake3/blake3_avx512_x86-64_unix.S
  $ar rs build/blake3.a build/blake3_*_unix.o
fi

# --- Set up Oodle SDK -------------------------------------------------------
if [[ "${oodle:-0}" == "1" ]]; then
  oodle_sdk_path="${OODLE_SDK_DIR:-}"
  if [[ -z "$oodle_sdk_path" ]]; then
    for sdk in "$PWD"/local/oodle2*; do
      if [[ -f "$sdk/linux/include/oodle2.h" ]]; then
        if [[ -n "$oodle_sdk_path" ]]; then
          echo "Multiple Oodle SDKs found: $oodle_sdk_path and $sdk/linux" >&2
          echo "Set OODLE_SDK_DIR to select the SDK root." >&2
          exit 1
        fi
        oodle_sdk_path="$sdk/linux"
      fi
    done
  fi
  if [[ -z "$oodle_sdk_path" ]]; then
    echo "Oodle SDK not found. Put an oodle2* SDK with a linux subdirectory in $PWD/local or set OODLE_SDK_DIR." >&2
    exit 1
  fi
  if [[ -f "$oodle_sdk_path/linux/include/oodle2.h" ]]; then
    oodle_sdk_path="$oodle_sdk_path/linux"
  fi
  if [[ ! -f "$oodle_sdk_path/include/oodle2.h" ]]; then
    echo "Oodle SDK directory '$oodle_sdk_path' must contain include/oodle2.h" >&2
    exit 1
  fi
  oodle_sdk_path="$(cd "$oodle_sdk_path" && pwd)"
  echo "[Oodle SDK: $oodle_sdk_path]"
  # Keep the include path as one argument, including when it contains spaces.
  oodle_flags=(-DOODLE_SDK=1 "-I$oodle_sdk_path/include")
  for library in "$oodle_sdk_path"/lib/liboo2corelinux64.so "$oodle_sdk_path"/lib/liboo2corelinux64.so.*; do
    if [[ -f "$library" ]]; then
      cp -- "$library" "$PWD/build/"
    fi
  done
fi

# --- Build Everything (@build_targets) ---------------------------------------
cd build
if [[ "${raddbg:-0}"               == "1" ]]; then didbuild=1 && compile $cc_graphical_language ../src/raddbg/raddbg_main.c $cc_icon $cc_link $cc_os_gfx $cc_render $cc_font_provider -o raddbg; fi
if [[ "${raddbg_non_graphical:-0}" == "1" ]]; then didbuild=1 && compile $cc_graphical_language ../src/raddbg/raddbg_main.c -DWM_STUB=1 -DR_BACKEND=R_BACKEND_STUB $cc_link $cc_os_gfx $cc_render $cc_font_provider -o raddbg_non_graphical; fi
if [[ "${radbin:-0}"               == "1" ]]; then didbuild=1 && compile ../src/radbin/radbin_main.c   $cc_link -o radbin; fi
if [[ "${radlink:-0}"              == "1" ]]; then didbuild=1 && compile ../src/linker/lnk.c           $cc_link -o radlink; fi
if [[ "${torture:-0}"              == "1" ]]; then didbuild=1 && compile $cc_graphical_language ../src/torture/torture_main.c $cc_link $cc_os_gfx $cc_render $cc_font_provider -o torture; fi
if [[ "$host_os" == "Darwin" ]]; then
  for target in raddbg raddbg_non_graphical; do
    if [[ "${!target:-0}" == "1" ]]; then
      codesign --force --sign - --entitlements ../src/mac/raddbg.entitlements "$target"
    fi
  done
  if [[ "${raddbg:-0}" == "1" ]]; then
    mkdir -p raddbg.app/Contents/MacOS
    cp raddbg raddbg.app/Contents/MacOS/raddbg
    cp ../src/mac/Info.plist raddbg.app/Contents/Info.plist
    codesign --force --sign - --entitlements ../src/mac/raddbg.entitlements raddbg.app
  fi
fi
cd ..

# --- Warn On No Builds -------------------------------------------------------
if [[ "${didbuild:-0}" == "0" ]]; then
  echo "[WARNING] no valid build target specified; must use build target names as arguments to this script, like \`./build.sh raddbg\` or \`./build.sh radlink\`."
  exit 1
fi

# --- Warn On Debug Builds (if debug not explicitly specified) ----------------
if [[ "${debug:-0}" != "1" && "${release:-0}" != "1" ]]; then
  echo "[INFO] Debug build complete. For a faster build, call this script with the \`release\` argument (this will take significantly longer than a debug build)."
fi
