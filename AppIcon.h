#ifndef APPICON_H
#define APPICON_H

#include <QIcon>
#include <QPixmap>
#include <QString>

namespace appui {

/** 程序/窗口/对话框统一图标（资源内为 img/appicon.png） */
inline QIcon appIcon()
{
    return QIcon(QStringLiteral(":/img/appicon.png"));
}

/** 标题栏用图标；按高度等比缩放，避免非方形图标被拉伸 */
inline QPixmap appIconPixmap(int height)
{
    const QPixmap source(QStringLiteral(":/img/appicon.png"));
    if (source.isNull() || height <= 0) {
        return QPixmap();
    }
    return source.scaledToHeight(height, Qt::SmoothTransformation);
}

} // namespace appui

#endif // APPICON_H
