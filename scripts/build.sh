#exit immediately if an error happens
set -e

# also exit if any command in a pipe fails
set -o pipefail

set -x

# Get script location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# set project_root to parent of location
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# cd to project root
cd "$PROJECT_ROOT"

echo "Building Leftover Pasta!!!"

# create build directory

mkdir -p build

# Build SDL3
echo "Building SDL3!"
mkdir -p "$PROJECT_ROOT/build/sdl3-build"
cd "$PROJECT_ROOT/build/sdl3-build"
cmake "$PROJECT_ROOT/dependencies/SDL3" -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON
make -j$(nproc)

# Build Rive + Renderer
cd "$PROJECT_ROOT"
mkdir -p "$PROJECT_ROOT/build/rive-build"
echo "Building Rive + Rive Renderer!"
cd "$PROJECT_ROOT/dependencies/rive-runtime/renderer"
RIVE_OUT="../../../build/rive-build" ../build/build_rive.sh release -- rive_pls_renderer rive_decoders rive_harfbuzz rive_sheenbidi rive_yoga libpng zlib libjpeg libwebp

# Build app
cd "$PROJECT_ROOT"
echo "Building LP App!"
cmake . -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo "Build complete!! Go play le game"