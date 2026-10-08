#include "settingsdialog.h"

#include "autostart.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

namespace {

const QString kRepoUrl = "https://github.com/joserodpt/rawPSense";

QLabel *label(const QString &text, const char *objectName = nullptr)
{
    auto *l = new QLabel(text);
    if (objectName)
        l->setObjectName(objectName);
    l->setWordWrap(true);
    return l;
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

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("rawPSense settings");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    QVBoxLayout *startup;
    QFrame *startupCard = card("STARTUP", startup);
    m_autostart = new QCheckBox("Start rawPSense when I sign in");
    m_autostart->setCursor(Qt::PointingHandCursor);
    m_autostart->setChecked(Autostart::isEnabled());
    connect(m_autostart, &QCheckBox::toggled, this, &SettingsDialog::onAutostart);
#ifdef Q_OS_WIN
    const char *hint = "Starts minimised in the tray, with administrator rights and no UAC prompt.";
#else
    const char *hint = "Starts minimised in the tray. Turbo needs root, so it stays unavailable when "
                       "started this way.";
#endif
    m_error = label({}, "status");
    m_error->setProperty("level", "hot");
    // Selectable so the schtasks output can be copied (drag to select, or right-click > Copy)
    m_error->setTextFormat(Qt::PlainText);
    m_error->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_error->setCursor(Qt::IBeamCursor);
    m_error->hide();
    startup->addWidget(m_autostart);
    startup->addWidget(label(hint, "muted"));
    startup->addWidget(m_error);

    QVBoxLayout *about;
    QFrame *aboutCard = card("ABOUT", about);
    auto *link = label(QString("<a href=\"%1\" style=\"color:#5fe3ff;\">%2</a>")
                           .arg(kRepoUrl, QString(kRepoUrl).remove("https://")),
                       "midValue");
    link->setTextFormat(Qt::RichText);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    link->setOpenExternalLinks(true);
    about->addWidget(label("rawPSense " + QCoreApplication::applicationVersion(), "muted"));
    about->addWidget(link);

    auto *close = new QPushButton("Close");
    close->setObjectName("fanMode");
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(12);
    layout->addWidget(startupCard);
    layout->addWidget(aboutCard);
    layout->addLayout(buttons);
    setMinimumWidth(380);
}

void SettingsDialog::onAutostart(bool on)
{
    QString error;
    if (Autostart::setEnabled(on, error)) {
        m_error->hide();
        return;
    }
    m_error->setText(QString("Could not %1 start at sign-in: %2").arg(on ? "enable" : "disable", error));
    m_error->show();
    const QSignalBlocker block(m_autostart);
    m_autostart->setChecked(!on);
}
