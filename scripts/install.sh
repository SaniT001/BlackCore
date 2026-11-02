#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Установка BlackCore в систему (нужен sudo только для /usr/local).
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if [[ ! -x build/blackcore ]]; then
    echo "Сначала соберите проект: ./scripts/build.sh Release" >&2
    exit 1
fi

echo "==> Установка в $PREFIX"
sudo cmake --install build --prefix "$PREFIX"

echo "==> Обновление кэшей рабочего стола"
sudo update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
sudo gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true

cat <<EOF

Готово.
  Запуск из меню: «BlackCore» (категория «Система»)
  Запуск из терминала: $PREFIX/bin/blackcore
  Удаление: sudo xargs rm -v < build/install_manifest.txt
EOF
