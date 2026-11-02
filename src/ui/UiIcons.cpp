// SPDX-License-Identifier: MIT
#include "UiIcons.h"

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace BlackCore {
namespace {

QPixmap renderTinted(QSvgRenderer &renderer, int size, int scale, const QColor &color)
{
    QImage image(size * scale, size * scale, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), color);
    painter.end();

    QPixmap result = QPixmap::fromImage(image);
    result.setDevicePixelRatio(scale);
    return result;
}

} // namespace

QIcon UiIcons::create(const QString &name, const QColor &normal,
                      const QColor &hover, const QColor &disabled)
{
    QSvgRenderer renderer(QStringLiteral(":/ui-icons/%1.svg").arg(name));
    if (!renderer.isValid())
        return QIcon();

    QIcon icon;
    for (const int scale : {1, 2, 3}) {
        icon.addPixmap(renderTinted(renderer, kActionSize, scale, normal), QIcon::Normal);
        icon.addPixmap(renderTinted(renderer, kActionSize, scale, hover), QIcon::Active);
        icon.addPixmap(renderTinted(renderer, kActionSize, scale, disabled), QIcon::Disabled);
        icon.addPixmap(renderTinted(renderer, kActionSize, scale, hover), QIcon::Selected);
    }
    return icon;
}

} // namespace BlackCore
