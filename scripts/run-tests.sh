#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Автотесты этапа 1. Требуется дисплей; при отсутствии — используется xvfb-run.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

SHOTS_DIR="${1:-docs/screenshots}"

if [[ ! -x build/blackcore-tests ]]; then
    echo "==> Сборка тестов"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON >/dev/null
    cmake --build build -j"$(nproc)" >/dev/null
fi

export LANG="${LANG:-C.UTF-8}"
export LC_ALL="${LC_ALL:-C.UTF-8}"

if [[ -n "${DISPLAY:-}" ]]; then
    ./build/blackcore-tests --screenshots "$SHOTS_DIR"
elif command -v xvfb-run >/dev/null 2>&1; then
    echo "==> DISPLAY не задан, запуск через xvfb-run"
    xvfb-run -a ./build/blackcore-tests --screenshots "$SHOTS_DIR"
else
    echo "Нет графической сессии и не установлен xvfb-run (sudo apt install xvfb)." >&2
    exit 1
fi
