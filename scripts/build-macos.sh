#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset mac-universal
cmake --build --preset mac-universal
ctest --preset mac-universal
