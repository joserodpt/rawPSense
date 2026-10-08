#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#ifdef Q_OS_WIN
#include <QProcess>
#include <QTemporaryFile>
#include <windows.h>
#else
#include <QStandardPaths>
#include <pwd.h>
#include <unistd.h>
#endif

namespace {

#ifdef Q_OS_WIN

const QString kTaskName = "rawPSense";

// Console tools like schtasks write in the OEM code page (e.g. 850), not the ANSI one fromLocal8Bit uses
QString fromOemCodePage(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return {};
    const int length = MultiByteToWideChar(CP_OEMCP, 0, bytes.constData(), bytes.size(), nullptr, 0);
    QString text(length, Qt::Uninitialized);
    MultiByteToWideChar(CP_OEMCP, 0, bytes.constData(), bytes.size(),
                        reinterpret_cast<wchar_t *>(text.data()), length);
    return text;
}

bool schtasks(const QString &args, QString *error = nullptr)
{
    QProcess process;
    process.setProgram("schtasks.exe");
    // Native arguments so the quoting below reaches schtasks unchanged
    process.setNativeArguments(args);
    process.start();
    if (!process.waitForFinished(10000)) {
        if (error)
            *error = "schtasks did not respond";
        process.kill();
        return false;
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
        return true;
    if (error)
        *error = fromOemCodePage(process.readAllStandardError()).trimmed();
    return false;
}

// Created from XML because the plain schtasks options would leave the task's defaults in place:
// not starting on battery and being stopped after 3 days
QString taskXml()
{
    const QString user = qEnvironmentVariable("USERDOMAIN") + "\\" + qEnvironmentVariable("USERNAME");
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QString(R"(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>Starts rawPSense in the tray when you sign in</Description>
  </RegistrationInfo>
  <Triggers>
    <LogonTrigger>
      <Enabled>true</Enabled>
      <UserId>%1</UserId>
    </LogonTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>%1</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>%2</Command>
      <Arguments>--minimized</Arguments>
    </Exec>
  </Actions>
</Task>
)")
        .arg(user.toHtmlEscaped(), exe.toHtmlEscaped());
}

#else

struct AutostartPath {
    QString dir;
    QString file;
    // Owner to give the file to when running under sudo, -1 otherwise
    uid_t uid = uid_t(-1);
    gid_t gid = gid_t(-1);
};

// Under sudo HOME usually points at root, so use the invoking user's autostart folder instead
AutostartPath autostartPath()
{
    AutostartPath path;
    path.dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart";
    const QByteArray sudoUser = qgetenv("SUDO_USER");
    if (geteuid() == 0 && !sudoUser.isEmpty()) {
        if (const passwd *pw = getpwnam(sudoUser.constData())) {
            path.dir = QString::fromLocal8Bit(pw->pw_dir) + "/.config/autostart";
            path.uid = pw->pw_uid;
            path.gid = pw->pw_gid;
        }
    }
    path.file = path.dir + "/rawPSense.desktop";
    return path;
}

#endif

} // namespace

namespace Autostart {

#ifdef Q_OS_WIN

bool isEnabled()
{
    return schtasks(QString("/Query /TN \"%1\"").arg(kTaskName));
}

bool setEnabled(bool on, QString &error)
{
    if (!on)
        return schtasks(QString("/Delete /F /TN \"%1\"").arg(kTaskName), &error);

    // Not a QTemporaryFile: its close() keeps the handle open, so schtasks can't read the file
    QString path;
    {
        QTemporaryFile temp(QDir::tempPath() + "/rawPSense-XXXXXX.xml");
        if (!temp.open()) {
            error = "Cannot create temporary file: " + temp.errorString();
            return false;
        }
        path = temp.fileName();
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = "Cannot write " + QDir::toNativeSeparators(path) + ": " + file.errorString();
        return false;
    }
    // schtasks expects UTF-16 with a byte order mark
    const QString xml = taskXml();
    QByteArray data("\xFF\xFE", 2);
    data.append(reinterpret_cast<const char *>(xml.utf16()), xml.size() * 2);
    file.write(data);
    file.close();
    const bool ok = schtasks(QString("/Create /F /TN \"%1\" /XML \"%2\"")
                                 .arg(kTaskName, QDir::toNativeSeparators(path)),
                             &error);
    QFile::remove(path);
    return ok;
}

#else

bool isEnabled()
{
    return QFile::exists(autostartPath().file);
}

bool setEnabled(bool on, QString &error)
{
    const AutostartPath path = autostartPath();
    if (!on) {
        if (QFile::exists(path.file) && !QFile::remove(path.file)) {
            error = "Cannot remove " + path.file;
            return false;
        }
        return true;
    }

    const bool dirExisted = QDir(path.dir).exists();
    if (!QDir().mkpath(path.dir)) {
        error = "Cannot create " + path.dir;
        return false;
    }
    QFile file(path.file);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = "Cannot write " + path.file + ": " + file.errorString();
        return false;
    }
    file.write(QString("[Desktop Entry]\n"
                       "Type=Application\n"
                       "Name=rawPSense\n"
                       "Comment=Turbo, fan and temperature control for Acer Predator laptops\n"
                       "Exec=\"%1\" --minimized\n"
                       "Icon=rawpsense\n"
                       "X-GNOME-Autostart-enabled=true\n")
                   .arg(QCoreApplication::applicationFilePath())
                   .toUtf8());
    file.close();

    if (path.uid != uid_t(-1)) {
        if (!dirExisted)
            (void)chown(QFile::encodeName(path.dir).constData(), path.uid, path.gid);
        (void)chown(QFile::encodeName(path.file).constData(), path.uid, path.gid);
    }
    return true;
}

#endif

} // namespace Autostart
