// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace BlackCore {

//! Unified, theme-aware SVG icons for BlackCore controls.
class UiIcons
{
public:
    static constexpr int kActionSize = 18;

    //! Creates normal, hover and disabled variants from an embedded SVG asset.
    static QIcon create(const QString &name, const QColor &normal,
                        const QColor &hover, const QColor &disabled);
};

} // namespace BlackCore
