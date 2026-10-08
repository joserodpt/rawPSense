#pragma once

#include "backend.h"

#include <QTimer>
#include <QWidget>

class QAction;
class QButtonGroup;
class QSystemTrayIcon;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;

class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct SensorTile {
        QLabel *temp = nullptr;
        QProgressBar *bar = nullptr;
        QLabel *rpm = nullptr;
    };
    struct FanRow {
        QButtonGroup *mode = nullptr;
        QSlider *slider = nullptr;
        QLabel *value = nullptr;
    };

    QWidget *createSensorTile(const QString &title, SensorTile &tile, bool hasFan);
    QWidget *createTurbo();
    QWidget *createFans();
    void createTray();

    void showFromTray();
    void hideToTray();
    void openSettings();
    void refresh();
    void syncFans();
    void applyFan(int index);
    void onTurbo(bool on);
    void setStatus(const QString &text, bool error = false);

    std::unique_ptr<Backend> m_backend;
    SensorTile m_cpu;
    SensorTile m_gpu;
    SensorTile m_sys;
    QPushButton *m_turbo = nullptr;
    FanRow m_fans[2];
    QLabel *m_status = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_trayTurbo = nullptr;
    bool m_quitting = false;
    bool m_trayHintShown = false;
    QTimer m_timer;
};
