#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Приёмочные проверки BlackCore перед выпуском (этап 8).
#
#   ./scripts/qa.sh            # сборка + автотесты + статические проверки
#   ./scripts/qa.sh --quick    # без сборки (использует готовый build/)
#
# Результат дублируется в docs/qa-report.md.
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

QUICK=0
[[ "${1:-}" == "--quick" ]] && QUICK=1

REPORT="docs/qa-report.md"
mkdir -p docs

FAILED=0
ok()   { printf '  [ OK ] %s\n' "$1"; }
bad()  { printf '  [FAIL] %s\n' "$1"; FAILED=$((FAILED + 1)); }
head_() { printf '\n== %s ==\n' "$1"; }

: > "$REPORT"
{
    echo "# Отчёт приёмочных проверок BlackCore"
    echo
    echo "Дата: $(date '+%Y-%m-%d %H:%M')"
    echo "Система: $(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME")"
    echo "Ядро: $(uname -r)"
    echo "Qt: $(pkg-config --modversion Qt6Core 2>/dev/null || echo '?')"
    echo "libvterm: $(pkg-config --modversion vterm 2>/dev/null || echo '?')"
    echo
} >> "$REPORT"

dump() { printf '%s\n' "$1" >> "$REPORT"; }

head_ "1. Статические проверки исходников"
STATIC_FAILED=0
if grep -rIls --include=*.cpp --include=*.h -E 'QNetworkAccessManager|QNetworkRequest|QSslSocket' src/ >/dev/null; then
    bad "в исходниках нет сетевого кода"; STATIC_FAILED=1
else
    ok "в исходниках нет сетевого кода (телеметрия невозможна)"
fi
if grep -rIls --include=*.cpp --include=*.h -E 'https?://[a-z0-9.-]+' src/ | grep -v Secrets >/dev/null; then
    bad "в исходниках нет адресов для отправки данных"; STATIC_FAILED=1
else
    ok "в исходниках нет адресов отправки данных"
fi
if grep -rInsE --include=*.cpp '([^:a-zA-Z_]|^)(system|popen|exec[lv]p?e?)\s*\(' src/ \
        | grep -vE 'QLocale::system|QStandardPaths::|qEnvironmentVariable' | grep -q .; then
    bad "нет вызовов внешних команд вне PTY"; STATIC_FAILED=1
else
    ok "нет вызовов внешних команд вне PTY"
fi
if grep -rInsE --include=*.cpp '(\.bashrc|\.zshrc|\.profile|/etc/(passwd|shells))' src/ \
        | grep -E 'WriteOnly|QSaveFile|write\(|remove\(|rename\(' | grep -q .; then
    bad "приложение не пишет в файлы оболочки"; STATIC_FAILED=1
else
    ok "приложение не пишет в файлы оболочки и системные файлы"
fi
dump ""
if [[ $STATIC_FAILED -eq 0 ]]; then
    dump "- Статические проверки: пройдены (нет сетевого кода, нет записи в файлы оболочки)."
else
    dump "- Статические проверки: **есть замечания**."
fi

head_ "2. Сборка"
if [[ $QUICK -eq 1 ]]; then
    ok "сборка пропущена (--quick)"
    dump "- Сборка: пропущена (\`--quick\`)."
else
    BUILD_LOG="$(mktemp)"
    if ./scripts/build.sh Release --tests >"$BUILD_LOG" 2>&1; then
        WARNINGS=$(grep -ci "warning:" "$BUILD_LOG" || true)
        if [[ "$WARNINGS" -eq 0 ]]; then
            ok "сборка без предупреждений"
            dump "- Сборка: успешно, предупреждений нет."
        else
            bad "сборка с предупреждениями: $WARNINGS"
            dump "- Сборка: успешно, предупреждений: $WARNINGS."
        fi
    else
        bad "сборка не удалась (см. $BUILD_LOG)"
        dump "- Сборка: **ошибка**."
    fi
    rm -f "$BUILD_LOG"
fi

head_ "3. Автотесты"
if [[ -x build/blackcore-tests ]]; then
    export LC_ALL="${LC_ALL:-C.UTF-8}"
    export LANG="${LANG:-C.UTF-8}"
    TEST_LOG="$(mktemp)"
    if [[ -n "${DISPLAY:-}" ]]; then
        ./build/blackcore-tests --screenshots docs/screenshots/stage8 >"$TEST_LOG" 2>&1
    elif command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a ./build/blackcore-tests >"$TEST_LOG" 2>&1
    else
        echo "Нет графической сессии и xvfb-run" >&2
        exit 1
    fi
    SUMMARY=$(grep -E "^Проверок:" "$TEST_LOG" | tail -1 | sed 's/^Проверок: //; s/, провалов:.*//')
    FAILURES=$(grep -c "\[FAIL\]" "$TEST_LOG" || true)
    if [[ "$FAILURES" -eq 0 ]]; then
        ok "автотесты пройдены — $SUMMARY проверок, провалов: 0"
    else
        bad "провалов: $FAILURES — проверок: $SUMMARY"
        grep "\[FAIL\]" "$TEST_LOG" | head -20
    fi
    dump "- Автотесты: $SUMMARY проверок, провалов: $FAILURES."
    grep -E "^  \[ OK \] 8\.[123]" "$TEST_LOG" | head -80 | sed 's/^  /    /' >> "$REPORT"
    rm -f "$TEST_LOG"
else
    bad "build/blackcore-tests не найден — сначала соберите с -DBUILD_TESTING=ON"
    dump "- Автотесты: не запускались."
fi

head_ "4. Пакет"
if command -v cpack >/dev/null 2>&1; then
    ( cd build && cpack -G DEB >/dev/null 2>&1 )
    DEB=$(ls build/blackcore_*_amd64.deb 2>/dev/null | sort | tail -1)
    if [[ -n "$DEB" ]]; then
        ok "пакет собран: $DEB ($(stat -c%s "$DEB") байт)"
        dump "- Пакет: \`$(basename "$DEB")\`, $(stat -c%s "$DEB") байт."
        if command -v desktop-file-validate >/dev/null 2>&1; then
            DESKTOP_FILE=$(mktemp --suffix=.desktop)
            dpkg-deb --fsys-tarfile "$DEB" | tar -xO ./usr/share/applications/blackcore.desktop \
                > "$DESKTOP_FILE" 2>/dev/null
            if desktop-file-validate "$DESKTOP_FILE"; then
                ok "файл запуска проходит desktop-file-validate"
            else
                bad "файл запуска не прошёл проверку"
            fi
            rm -f "$DESKTOP_FILE"
        fi
    else
        bad "пакет не собран"
    fi
else
    bad "cpack не найден"
fi

head_ "Итог"
if [[ $FAILED -eq 0 ]]; then
    ok "все приёмочные проверки пройдены"
    dump ""
    dump "**Итог: все приёмочные проверки пройдены.**"
    STATUS=0
else
    bad "замечаний: $FAILED"
    dump ""
    dump "**Итог: замечаний — $FAILED.**"
    STATUS=1
fi
echo
echo "Отчёт: $REPORT"
exit $STATUS
