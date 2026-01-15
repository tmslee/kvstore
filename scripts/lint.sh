set -e

cd "$(dirname "$0")/.."

if [ ! -f build/compile_commands.json ]; then
    echo "Run cmake first to generate compile_commands.json"
    exit 1
fi

find include src -name '*.cpp' -o -name '*.hpp' | xargs clang-tidy -p build
echo "Linting complete."