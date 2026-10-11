#pragma once
#include <QObject>
#include <QWindow>

// 无边框窗口的平台增强:Windows 11 圆角与系统阴影 (DWM),其他平台为空操作
class WindowHelper : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void applyNativeFrame(QWindow* window);
    Q_INVOKABLE bool isWindows() const;
};
