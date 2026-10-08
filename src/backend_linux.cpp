#include "backend.h"

#include <QDir>
#include <QFile>
#include <QStringList>

namespace {

const QString kProfile = "/sys/firmware/acpi/platform_profile";
const QString kProfileChoices = "/sys/firmware/acpi/platform_profile_choices";

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

int readInt(const QString &path, int divisor = 1)
{
    bool ok = false;
    const int value = readFile(path).toInt(&ok);
    return ok ? value / divisor : -1;
}

// Uses what the kernel acer-wmi driver exposes:
//  - hwmon device named "acer": temp1 CPU, temp2 GPU, temp3 system, fan1 CPU, fan2 GPU
//  - ACPI platform_profile for turbo ("performance")
// The driver has no manual fan speed interface, so fan control is unavailable here.
class LinuxBackend : public Backend {
public:
    LinuxBackend()
    {
        const QDir hwmon("/sys/class/hwmon");
        for (const QString &entry : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString dir = hwmon.filePath(entry);
            const QString name = readFile(dir + "/name");
            if (name == "acer")
                m_acer = dir;
            else if (name == "coretemp" || name == "k10temp")
                m_cpu = dir;
        }

        m_choices = readFile(kProfileChoices).split(' ');
        m_choices.removeAll(QString());
        m_normal = m_choices.contains("balanced") ? "balanced" : m_choices.value(0);

        if (m_acer.isEmpty())
            m_error = "acer-wmi hwmon sensors not found (needs a recent kernel; try the acer_wmi.predator_v4=1 "
                      "module option)";
    }

    QString name() const override { return "Linux (acer-wmi)"; }
    bool isAvailable() const override { return !m_acer.isEmpty() || !m_cpu.isEmpty() || canTurbo(); }
    bool canTurbo() const override { return m_choices.contains("performance"); }
    bool canControlFans() const override { return false; }

    Readings readSensors() override
    {
        Readings r;
        if (!m_acer.isEmpty()) {
            r.cpuTemp = readInt(m_acer + "/temp1_input", 1000);
            r.gpuTemp = readInt(m_acer + "/temp2_input", 1000);
            r.sysTemp = readInt(m_acer + "/temp3_input", 1000);
            r.cpuRpm = readInt(m_acer + "/fan1_input");
            r.gpuRpm = readInt(m_acer + "/fan2_input");
        }
        if (r.cpuTemp < 0 && !m_cpu.isEmpty())
            r.cpuTemp = readInt(m_cpu + "/temp1_input", 1000);
        return r;
    }

    bool turbo(bool &on) override
    {
        const QString profile = readFile(kProfile);
        if (profile.isEmpty())
            return false;
        on = profile == "performance";
        return true;
    }

    bool setTurbo(bool on) override
    {
        QFile f(kProfile);
        if (!f.open(QIODevice::WriteOnly)) {
            m_error = "Cannot write " + kProfile + ": " + f.errorString() + " (run rawPSense as root)";
            return false;
        }
        if (f.write((on ? QString("performance") : m_normal).toUtf8()) < 0 || !f.flush()) {
            m_error = "Cannot set platform profile: " + f.errorString();
            return false;
        }
        return true;
    }

private:
    QString m_acer;
    QString m_cpu;
    QStringList m_choices;
    QString m_normal;
};

} // namespace

std::unique_ptr<Backend> Backend::create()
{
    return std::make_unique<LinuxBackend>();
}
