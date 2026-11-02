// SPDX-License-Identifier: MIT
#include "Secrets.h"

#include <QRegularExpression>
#include <QStringList>

namespace BlackCore {
namespace {

//! Шаблоны «ключ = значение» и «ключ: значение»
struct Rule
{
    const char *pattern;
    int valueGroup; //!< номер группы, которую нужно заменить
};

const Rule kRules[] = {
    // password=..., passwd: ..., PASSWORD = ...
    {R"((?i)\b([a-z0-9_-]*(?:password|passwd|pwd|secret|token|api[_-]?key|apikey|access[_-]?key|private[_-]?key|passphrase))(\s*[=:]\s*)(\S+))", 3},
    // --password=..., --token ...
    {R"((?i)(--?password|--?passwd|--?token|--?api-key|--?secret)(\s+|=)(\S+))", 3},
    // Authorization: Bearer xxx
    {R"((?i)(authorization\s*:\s*(?:bearer|basic|token)\s+)(\S+))", 2},
    // sshpass -p 'secret'
    {R"((?i)(sshpass\s+-p\s*)(\S+))", 2},
    // mysql -pSecret, psql "postgres://user:pass@host"
    {R"((?i)(\bmysql\b[^\n]*?\s-p)(\S+))", 2},
    // схема URL с паролем: postgres://user:pass@host, ssh://user:pass@host
    {R"((?i)([a-z][a-z0-9+.-]*://[^:/\s]+:)([^@/\s]+)(@))", 2},
    // user:pass@host
    {R"((?i)\b([a-z0-9._-]+:)([^\s@:]{3,})(@[a-z0-9.-]+))", 2},
};

//! Приватный ключ целиком — блок между BEGIN и END
const char *kPrivateKeyPattern =
    R"(-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----[\s\S]*?(-----END [A-Z0-9 ]*PRIVATE KEY-----|$))";

QString maskedValue(const QString &value)
{
    if (value.isEmpty())
        return value;
    return Secrets::mask();
}

} // namespace

QString Secrets::mask()
{
    return QStringLiteral("••••");
}

QStringList Secrets::patterns()
{
    QStringList list;
    for (const Rule &rule : kRules)
        list << QString::fromLatin1(rule.pattern);
    list << QString::fromLatin1(kPrivateKeyPattern);
    return list;
}

QString Secrets::redact(const QString &text)
{
    if (text.isEmpty())
        return text;

    QString result = text;

    // Приватные ключи вырезаем первыми: в них могут быть любые символы
    QRegularExpression key(QString::fromLatin1(kPrivateKeyPattern));
    result.replace(key, QStringLiteral("-----BEGIN PRIVATE KEY----- %1 -----END PRIVATE KEY-----")
                            .arg(mask()));

    for (const Rule &rule : kRules) {
        QRegularExpression expression(QString::fromLatin1(rule.pattern));
        // Заменяем только группу со значением, сохраняя «ключ» и разделитель
        QString output;
        int last = 0;
        auto it = expression.globalMatch(result);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const int start = match.capturedStart(rule.valueGroup);
            const int length = match.capturedLength(rule.valueGroup);
            if (start < 0 || length <= 0)
                continue;
            output += result.mid(last, start - last);
            output += maskedValue(match.captured(rule.valueGroup));
            last = start + length;
        }
        if (last > 0) {
            output += result.mid(last);
            result = output;
        }
    }

    return result;
}

bool Secrets::containsSecret(const QString &text)
{
    return redact(text) != text;
}

QString Secrets::commandForLog(const QString &command)
{
    return redact(command);
}

} // namespace BlackCore
