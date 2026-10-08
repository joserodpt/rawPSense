#include "mainwindow.h"

#include "settingsdialog.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QVBoxLayout>
#include <QVariant>

namespace {

constexpr int kRefreshMs = 2000;
// Keeps "Custom" from accidentally stopping a fan
constexpr int kMinFanPercent = 20;
constexpr int kWarmTemp = 75;
constexpr int kHotTemp = 90;

QLabel *label(const QString &text, const char *objectName = nullptr)
{
    auto *l = new QLabel(text);
    if (objectName)
        l->setObjectName(objectName);
    return l;
}

// Re-applies the stylesheet after changing a property used in a selector
void setLevel(QWidget *widget, const char *level)
{
    if (widget->property("level").toString() == level)
        return;
    widget->setProperty("level", level);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

QFrame *card(const QString &title, QVBoxLayout *&body)
{
    auto *frame = new QFrame;
    frame->setObjectName("card");
    body = new QVBoxLayout(frame);
    body->setContentsMargins(16, 12, 16, 14);
    body->setSpacing(8);
    body->addWidget(label(title, "cardTitle"));
    return frame;
}

// Same lightning bolt as the app icon
QPixmap boltPixmap(const QColor &color)
{
    const qreal scale = 2.0;
    QPixmap pixmap(QSize(56, 84) * scale);
    pixmap.fill(Qt::transparent);
    QPainterPath path;
    path.moveTo(31, 0);
    path.lineTo(0, 48);
    path.lineTo(20, 48);
    path.lineTo(13, 84);
    path.lineTo(48, 33);
    path.lineTo(27, 33);
    path.lineTo(36, 0);
    path.closeSubpath();
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(scale, scale);
    p.translate(4, 0);
    p.fillPath(path, color);
    return pixmap;
}

void showTemp(QLabel *label, QProgressBar *bar, int celsius)
{
    const char *level = celsius >= kHotTemp ? "hot" : celsius >= kWarmTemp ? "warm" : "normal";
    label->setText(celsius < 0 ? QString("--") : QString::number(celsius) + QChar(0x00B0) + "C");
    bar->setValue(qBound(0, celsius, 100));
    setLevel(label, level);
    setLevel(bar, level);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , m_backend(Backend::create())
{
    setWindowTitle("rawPSense");

    auto *sensors = new QHBoxLayout;
    sensors->setSpacing(12);
    sensors->addWidget(createSensorTile("CPU", m_cpu, true));
    sensors->addWidget(createSensorTile("GPU", m_gpu, true));
    sensors->addWidget(createSensorTile("SYSTEM", m_sys, false));

    m_status = label({}, "status");
    m_status->setWordWrap(true);

    auto *settings = new QPushButton("Settings");
    settings->setObjectName("fanMode");
    settings->setCursor(Qt::PointingHandCursor);
    connect(settings, &QPushButton::clicked, this, &MainWindow::openSettings);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(12);
    footer->addWidget(m_status, 1);
    footer->addWidget(settings);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(12);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    layout->addLayout(sensors);
    layout->addWidget(createTurbo());
    layout->addWidget(createFans());
    layout->addLayout(footer);

    if (!m_backend->isAvailable() || !m_backend->lastError().isEmpty())
        setStatus(m_backend->lastError(), !m_backend->isAvailable());

    createTray();
    syncFans();
    refresh();
    connect(&m_timer, &QTimer::timeout, this, &MainWindow::refresh);
    m_timer.start(kRefreshMs);
}

QWidget *MainWindow::createSensorTile(const QString &title, SensorTile &tile, bool hasFan)
{
    QVBoxLayout *body;
    QFrame *frame = card(title, body);
    tile.temp = label("--", "bigValue");
    tile.bar = new QProgressBar;
    tile.bar->setRange(0, 100);
    tile.bar->setTextVisible(false);
    tile.rpm = label(hasFan ? "--" : "", "midValue");
    auto *fanCaption = label(hasFan ? "fan speed" : "chassis sensor", "muted");
    body->addWidget(tile.temp);
    body->addWidget(tile.bar);
    body->addSpacing(4);
    body->addWidget(tile.rpm);
    body->addWidget(fanCaption);
    frame->setMinimumWidth(170);
    return frame;
}

QWidget *MainWindow::createTurbo()
{
    QVBoxLayout *body;
    QFrame *frame = card("TURBO", body);

    QIcon bolt;
    bolt.addPixmap(boltPixmap(QColor("#c2c9d8")), QIcon::Normal, QIcon::Off);
    bolt.addPixmap(boltPixmap(Qt::white), QIcon::Normal, QIcon::On);
    bolt.addPixmap(boltPixmap(QColor("#4a5266")), QIcon::Disabled, QIcon::Off);

    m_turbo = new QPushButton(" TURBO");
    m_turbo->setObjectName("turbo");
    m_turbo->setCheckable(true);
    m_turbo->setCursor(Qt::PointingHandCursor);
    m_turbo->setIcon(bolt);
    m_turbo->setIconSize(QSize(22, 32));
    if (!m_backend->canTurbo()) {
        m_turbo->setEnabled(false);
        m_turbo->setToolTip("Turbo control is not available on this system");
    }
    connect(m_turbo, &QPushButton::toggled, this, &MainWindow::onTurbo);

    auto *hint = label("Max fans + GPU overclock. Without a battery, heavy load can exceed the charger "
                       "and shut the laptop off.",
                       "muted");
    hint->setWordWrap(true);

    body->addWidget(m_turbo);
    body->addWidget(hint);
    return frame;
}

QWidget *MainWindow::createFans()
{
    QVBoxLayout *body;
    QFrame *frame = card("FAN CONTROL", body);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(10);
    body->addLayout(grid);

    const char *fanNames[] = {"CPU", "GPU"};
    const struct {
        const char *text;
        FanMode mode;
    } modes[] = {{"Auto", FanMode::Auto}, {"Max", FanMode::Max}, {"Custom", FanMode::Custom}};

    for (int i = 0; i < 2; ++i) {
        FanRow &row = m_fans[i];

        auto *segments = new QHBoxLayout;
        segments->setSpacing(8);
        row.mode = new QButtonGroup(this);
        for (const auto &m : modes) {
            auto *button = new QPushButton(m.text);
            button->setObjectName("fanMode");
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            row.mode->addButton(button, int(m.mode));
            segments->addWidget(button);
        }
        row.mode->button(int(FanMode::Auto))->setChecked(true);

        row.slider = new QSlider(Qt::Horizontal);
        row.slider->setRange(kMinFanPercent, 100);
        row.slider->setSingleStep(5);
        row.slider->setPageStep(10);
        row.slider->setValue(50);
        row.slider->setEnabled(false);
        row.slider->setMinimumWidth(150);
        row.value = label(QString("%1%").arg(row.slider->value()), "midValue");
        row.value->setMinimumWidth(52);
        row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        grid->addWidget(label(fanNames[i], "midValue"), i, 0);
        grid->addLayout(segments, i, 1);
        grid->addWidget(row.slider, i, 2);
        grid->addWidget(row.value, i, 3);

        // buttonClicked only fires on user clicks, so syncFans() can set the checked button freely
        connect(row.mode, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this,
                [this, i] { applyFan(i); });
        connect(row.slider, &QSlider::valueChanged, this, [this, i](int value) {
            m_fans[i].value->setText(QString("%1%").arg(value));
            if (!m_fans[i].slider->isSliderDown())
                applyFan(i);
        });
        connect(row.slider, &QSlider::sliderReleased, this, [this, i] { applyFan(i); });
    }
    grid->setColumnStretch(2, 1);

    if (!m_backend->canControlFans()) {
        frame->setEnabled(false);
        frame->setToolTip("Manual fan control is not available on this system");
    }
    return frame;
}

// Closing or minimising keeps the app running in the system tray; "Quit" in the tray menu exits.
// Falls back to normal window behaviour when the desktop has no tray (e.g. GNOME without an extension).
void MainWindow::createTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    QApplication::setQuitOnLastWindowClosed(false);

    auto *menu = new QMenu(this);
    menu->addAction("Show rawPSense", this, &MainWindow::showFromTray);
    menu->addSeparator();
    m_trayTurbo = menu->addAction("Turbo");
    m_trayTurbo->setCheckable(true);
    m_trayTurbo->setEnabled(m_backend->canTurbo());
    connect(m_trayTurbo, &QAction::toggled, m_turbo, &QPushButton::setChecked);
    menu->addSeparator();
    menu->addAction("Settings...", this, [this] {
        showFromTray();
        openSettings();
    });
    menu->addAction("Quit", this, [this] {
        m_quitting = true;
        QApplication::quit();
    });

    m_tray = new QSystemTrayIcon(windowIcon().isNull() ? QApplication::windowIcon() : windowIcon(), this);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip("rawPSense");
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
            return;
        if (isVisible() && !isMinimized())
            hideToTray();
        else
            showFromTray();
    });
    m_tray->show();
}

void MainWindow::showFromTray()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::hideToTray()
{
    hide();
    if (!m_trayHintShown) {
        m_trayHintShown = true;
        m_tray->showMessage("rawPSense", "Still running in the tray. Right-click the icon to quit.",
                            QSystemTrayIcon::Information, 3000);
    }
}

void MainWindow::openSettings()
{
    // The tray menu stays usable while the dialog is open
    if (auto *open = findChild<SettingsDialog *>()) {
        open->raise();
        open->activateWindow();
        return;
    }
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_tray && !m_quitting) {
        event->ignore();
        hideToTray();
        return;
    }
    QWidget::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // Hiding directly inside the state change confuses some window managers, so defer it
    if (m_tray && event->type() == QEvent::WindowStateChange && isMinimized())
        QTimer::singleShot(0, this, &MainWindow::hideToTray);
}

void MainWindow::refresh()
{
    const Readings r = m_backend->readSensors();
    showTemp(m_cpu.temp, m_cpu.bar, r.cpuTemp);
    showTemp(m_gpu.temp, m_gpu.bar, r.gpuTemp);
    showTemp(m_sys.temp, m_sys.bar, r.sysTemp);
    m_cpu.rpm->setText(r.cpuRpm < 0 ? QString("--") : QString("%1 RPM").arg(r.cpuRpm));
    m_gpu.rpm->setText(r.gpuRpm < 0 ? QString("--") : QString("%1 RPM").arg(r.gpuRpm));

    // Picks up changes made elsewhere, e.g. the physical turbo key
    bool on = false;
    if (m_backend->canTurbo() && m_backend->turbo(on) && on != m_turbo->isChecked()) {
        const QSignalBlocker block(m_turbo);
        m_turbo->setChecked(on);
        syncFans();
    }

    if (m_tray) {
        const QSignalBlocker block(m_trayTurbo);
        m_trayTurbo->setChecked(m_turbo->isChecked());
        const auto line = [](const char *name, int temp, int rpm) {
            QString text = QString("%1 %2").arg(name, temp < 0 ? QString("--") : QString::number(temp) + QChar(0x00B0) + "C");
            if (rpm >= 0)
                text += QString(" | %1 RPM").arg(rpm);
            return text;
        };
        m_tray->setToolTip(QString("rawPSense%1\n%2\n%3")
                               .arg(m_turbo->isChecked() ? " - TURBO" : "", line("CPU", r.cpuTemp, r.cpuRpm),
                                    line("GPU", r.gpuTemp, r.gpuRpm)));
    }
}

void MainWindow::syncFans()
{
    if (!m_backend->canControlFans())
        return;
    for (int i = 0; i < 2; ++i) {
        FanMode mode;
        int percent;
        if (!m_backend->fanMode(Fan(i), mode, percent))
            continue;
        FanRow &row = m_fans[i];
        if (QAbstractButton *button = row.mode->button(int(mode)))
            button->setChecked(true);
        const QSignalBlocker block(row.slider);
        if (mode == FanMode::Custom)
            row.slider->setValue(percent);
        row.slider->setEnabled(mode == FanMode::Custom);
        row.value->setText(QString("%1%").arg(row.slider->value()));
    }
}

void MainWindow::applyFan(int index)
{
    FanRow &row = m_fans[index];
    const auto mode = FanMode(row.mode->checkedId());
    row.slider->setEnabled(mode == FanMode::Custom);
    if (!m_backend->setFanMode(Fan(index), mode, row.slider->value())) {
        setStatus("Could not set fan: " + m_backend->lastError(), true);
        return;
    }
    setStatus(QString("%1 fan: %2%3")
                  .arg(index == 0 ? "CPU" : "GPU", row.mode->checkedButton()->text(),
                       mode == FanMode::Custom ? QString(" %1%").arg(row.slider->value()) : QString()));
}

void MainWindow::onTurbo(bool on)
{
    if (!m_backend->setTurbo(on)) {
        setStatus("Could not switch turbo: " + m_backend->lastError(), true);
        const QSignalBlocker block(m_turbo);
        m_turbo->setChecked(!on);
        return;
    }
    setStatus(on ? "Turbo on" : "Turbo off");
    if (m_trayTurbo) {
        const QSignalBlocker block(m_trayTurbo);
        m_trayTurbo->setChecked(on);
    }
    syncFans();
}

void MainWindow::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    setLevel(m_status, error ? "hot" : "normal");
}
