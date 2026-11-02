// SPDX-License-Identifier: MIT
// BlackCore — терминал для Linux Mint
#pragma once

#include <QString>

namespace BlackCore {

inline constexpr const char *kAppName = "BlackCore";
#ifndef BLACKCORE_VERSION
#error "BLACKCORE_VERSION must be defined by CMake"
#endif
inline constexpr const char *kAppVersion = BLACKCORE_VERSION;
inline constexpr const char *kAppStage = "Stable";
inline constexpr const char *kOrgName = "BlackCore";
inline constexpr const char *kOrgDomain = "blackcore.local";
inline constexpr const char *kAppId = "blackcore";

//! "1.0.0 Stable"
QString versionString();

//! "BlackCore 1.0.0 Stable"
QString fullName();

//! Строка для --version / логов
QString buildInfo();

//! Каталог состояния: $XDG_STATE_HOME/blackcore (по умолчанию ~/.local/state/blackcore).
/*!
 *  Внутри: журнал blackcore.log, маркер запуска, отчёты о сбоях и резервные
 *  копии настроек. Каталог создаётся при обращении.
 */
QString stateDirectory();

} // namespace BlackCore
