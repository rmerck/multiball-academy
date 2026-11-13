#!/bin/sh
# Prints one value from the product.env file CMake writes into a build
# directory (name, identifier, version and so on: see CMakeLists.txt).
# Usage: product_value.sh <product.env> <NAME>
#
# The file is read as text, never run: a line must be exactly NAME='value',
# once, with no quote inside the value, or this fails.
set -eu

[ "$#" -eq 2 ] || { echo "usage: product_value.sh <product.env> <NAME>" >&2; exit 1; }
file=$1
name=$2
[ -f "$file" ] || { echo "product_value: no such file: $file" >&2; exit 1; }
case $name in
  PINBALL_[A-Z_]*) ;;
  *) echo "product_value: not a product name: $name" >&2; exit 1 ;;
esac
found=$(grep -c "^$name='[^']*'\$" "$file" || true)
[ "$found" = 1 ] || { echo "product_value: $name is not set exactly once in $file" >&2; exit 1; }
sed -n "s/^$name='\\([^']*\\)'\$/\\1/p" "$file"
