set -e

find include src tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
echo "Formatting complete."