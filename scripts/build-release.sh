#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Сборка релиза BlackCore: пакет .deb, архив исходников и контрольные суммы.
#
#   ./scripts/build-release.sh            # полная сборка релиза
#   ./scripts/build-release.sh --skip-tests
#
# Результат: каталог release/
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERSION="$(sed -n 's/^project(BlackCore VERSION \([0-9.]*\) LANGUAGES CXX).*/\1/p' CMakeLists.txt)"
if [[ -z "$VERSION" ]]; then
    echo "Не удалось определить версию из CMakeLists.txt" >&2
    exit 1
fi

RELEASE_DIR="release"
SKIP_TESTS=0
[[ "${1:-}" == "--skip-tests" ]] && SKIP_TESTS=1

echo "==> BlackCore $VERSION: подготовка релиза"

# --- 1. сборка и тесты ------------------------------------------------------
if [[ $SKIP_TESTS -eq 1 ]]; then
    echo "==> Сборка (без тестов)"
    ./scripts/build.sh Release
else
    echo "==> Сборка и автотесты"
    ./scripts/build.sh Release --tests
    export LC_ALL="${LC_ALL:-C.UTF-8}"
    export LANG="${LANG:-C.UTF-8}"
    if [[ -n "${DISPLAY:-}" ]]; then
        ./build/blackcore-tests
    elif command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a ./build/blackcore-tests
    else
        echo "Нет графической сессии и xvfb-run — тесты пропущены" >&2
    fi
fi

# --- 2. пакет .deb ---------------------------------------------------------
echo "==> Пакет Debian"
rm -f build/blackcore_*_amd64.deb
( cd build && cpack -G DEB >/dev/null )
DEB="build/blackcore_${VERSION}_amd64.deb"
[[ -f "$DEB" ]] || { echo "Пакет не собран" >&2; exit 1; }

# --- 3. архив исходников ---------------------------------------------------
echo "==> Архив исходников"
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"
ARCHIVE="$RELEASE_DIR/blackcore-${VERSION}.tar.gz"
tar --create --gzip --file "$ARCHIVE" \
    --directory="$(dirname "$ROOT_DIR")" \
    --exclude='build' --exclude='release' --exclude='.git' --exclude='*.deb' \
    --transform "s,^$(basename "$ROOT_DIR"),blackcore-${VERSION}," \
    "$(basename "$ROOT_DIR")"

# --- 4. документация и контрольные суммы ----------------------------------
echo "==> Контрольные суммы"
cp "$DEB" "$RELEASE_DIR/"
cp CHANGELOG.md README.md LICENSE "$RELEASE_DIR/"
cp docs/qa-report.md "$RELEASE_DIR/qa-report.md" 2>/dev/null || true

( cd "$RELEASE_DIR" && sha256sum ./*.deb ./*.tar.gz ./*.md ./LICENSE > SHA256SUMS )

echo
echo "==> Готово: $RELEASE_DIR/"
ls -lh "$RELEASE_DIR" | sed 's/^/    /'
echo
echo "    Проверка сумм: cd $RELEASE_DIR && sha256sum -c SHA256SUMS"
echo "    Установка:     sudo apt install ./blackcore_${VERSION}_amd64.deb"
