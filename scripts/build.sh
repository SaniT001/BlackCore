#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Сборка BlackCore: ./scripts/build.sh [Release|Debug] [--tests]
set -euo pipefail

BUILD_TYPE="${1:-Release}"
shift || true
TESTS=OFF
for arg in "$@"; do
    [[ "$arg" == "--tests" ]] && TESTS=ON
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Проверка зависимостей до запуска cmake — понятные сообщения вместо ошибок CMake
missing=()
command -v cmake >/dev/null 2>&1 || missing+=("cmake")
command -v g++   >/dev/null 2>&1 || missing+=("g++")
pkg-config --exists Qt6Widgets 2>/dev/null || missing+=("qt6-base-dev")
pkg-config --exists vterm     2>/dev/null || missing+=("libvterm-dev")

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "Не хватает зависимостей: ${missing[*]}" >&2
    echo "Установите: sudo apt install build-essential cmake qt6-base-dev libvterm-dev pkg-config" >&2
    exit 1
fi

GENERATOR=()
command -v ninja >/dev/null 2>&1 && GENERATOR=(-G Ninja)

echo "==> Конфигурация ($BUILD_TYPE, тесты: $TESTS)"
cmake -S . -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DBUILD_TESTING="$TESTS" "${GENERATOR[@]}"

echo "==> Сборка"
cmake --build build -j"$(nproc)"

echo "==> Готово: $ROOT_DIR/build/blackcore"
echo "    Запуск: ./build/blackcore"
echo "    Установка: sudo cmake --install build"
