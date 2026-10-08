#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    void onAutostart(bool on);

    QCheckBox *m_autostart = nullptr;
    QLabel *m_error = nullptr;
};
