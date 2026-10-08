#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QStyleFactory>
#include <QSystemTrayIcon>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QApplication::setApplicationName("rawPSense");
    QApplication::setApplicationVersion(RAWPSENSE_VERSION);
    QApplication::setWindowIcon(QIcon(":/icon.png"));

    // Fusion renders the stylesheet the same on Windows and Linux
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QFile style(":/style.qss");
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    // One instance only, two would fight over the fan and turbo settings. A lock left by a crashed
    // instance is detected through its PID and taken over.
    QLockFile lock(QDir::tempPath() + "/rawPSense.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) {
        QMessageBox::warning(nullptr, "rawPSense",
                             "rawPSense is already running.\n\n"
                             "Look for its icon in the system tray.");
        return 1;
    }

    MainWindow window;
    // Passed when started at sign-in, see autostart.h
    if (!app.arguments().contains("--minimized") || !QSystemTrayIcon::isSystemTrayAvailable())
        window.show();
    return app.exec();
}
