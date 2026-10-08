#pragma once

#include <QString>
#include <memory>

enum class Fan { Cpu = 0, Gpu = 1 };
enum class FanMode { Auto = 1, Max = 2, Custom = 3 };

// -1 means the value is not available on this machine
struct Readings {
    int cpuTemp = -1;
    int gpuTemp = -1;
    int sysTemp = -1;
    int cpuRpm = -1;
    int gpuRpm = -1;
};

// Platform-specific access to the laptop's gaming functions.
// Windows talks to the AcerGamingFunction WMI class, Linux to the acer-wmi driver's sysfs files.
class Backend {
public:
    virtual ~Backend() = default;

    virtual QString name() const = 0;
    virtual bool isAvailable() const = 0;
    virtual bool canTurbo() const = 0;
    virtual bool canControlFans() const = 0;

    virtual Readings readSensors() = 0;
    virtual bool turbo(bool &on) = 0;
    virtual bool setTurbo(bool on) = 0;
    virtual bool fanMode(Fan, FanMode &, int &) { return false; }
    virtual bool setFanMode(Fan, FanMode, int) { return false; }

    QString lastError() const { return m_error; }

    static std::unique_ptr<Backend> create();

protected:
    QString m_error;
};
