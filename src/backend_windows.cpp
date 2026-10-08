#include "backend.h"

#define _WIN32_DCOM
#include <windows.h>
#include <comdef.h>
#include <wbemidl.h>

#include <string>

namespace {

// Values below were verified on a Predator PH315-53 and match the Linux acer-wmi driver.

// GetGamingSysInfo sensor ids: command = 0x1 | (id << 8)
// result bits 0-7 = status (0 = ok), temperature in bits 8-15, fan RPM in bits 8-23
enum Sensor : quint32 { CpuTemp = 0x01, CpuFan = 0x02, SysTemp = 0x03, GpuFan = 0x06, GpuTemp = 0x0A };

// SetGamingFanBehavior: fan mask in the low bits, mode (1 auto, 2 max, 3 custom) shifted per fan
constexpr quint32 kBehaviorBit[] = {0x1, 0x8};
constexpr int kBehaviorShift[] = {16, 22};
// GetGamingFanBehavior(0x9) returns the modes at these shifts
constexpr int kBehaviorReadShift[] = {8, 14};
// SetGamingFanSpeed / GetGamingFanSpeed fan index
constexpr quint32 kSpeedIndex[] = {0x1, 0x4};

QString hrText(HRESULT hr)
{
    return QString("0x%1 %2")
        .arg(quint32(hr), 8, 16, QChar('0'))
        .arg(QString::fromWCharArray(_com_error(hr).ErrorMessage()));
}

class WindowsBackend : public Backend {
public:
    WindowsBackend() { m_ok = connect(); }

    ~WindowsBackend() override
    {
        if (m_class)
            m_class->Release();
        if (m_svc)
            m_svc->Release();
        if (m_comInit)
            CoUninitialize();
    }

    QString name() const override { return "Windows (AcerGamingFunction WMI)"; }
    bool isAvailable() const override { return m_ok; }
    bool canTurbo() const override { return m_ok; }
    bool canControlFans() const override { return m_ok; }

    Readings readSensors() override
    {
        Readings r;
        r.cpuTemp = sensor(CpuTemp, 0xFF);
        r.gpuTemp = sensor(GpuTemp, 0xFF);
        r.sysTemp = sensor(SysTemp, 0xFF);
        r.cpuRpm = sensor(CpuFan, 0xFFFF);
        r.gpuRpm = sensor(GpuFan, 0xFFFF);
        return r;
    }

    bool turbo(bool &on) override
    {
        quint64 out;
        if (!call(L"GetGamingLED", 0x1, out))
            return false;
        on = ((out >> 8) & 0xFF) != 0;
        return true;
    }

    // Same sequence the turbo key triggers, minus PredatorSense's battery check
    bool setTurbo(bool on) override
    {
        quint64 out;
        if (!call(L"SetGamingLED", on ? 0x10001 : 0x1, out))
            return false;
        if (!call(L"SetGamingFanBehavior", on ? 0x820009 : 0x410009, out))
            return false;
        // GPU overclock profile; these return the applied level, not a status code
        return call(L"SetGamingMiscSetting", on ? 0x205 : 0x5, out)
            && call(L"SetGamingMiscSetting", on ? 0x207 : 0x7, out);
    }

    bool fanMode(Fan fan, FanMode &mode, int &percent) override
    {
        const int i = int(fan);
        quint64 behavior, speed;
        if (!call(L"GetGamingFanBehavior", 0x9, behavior) || !call(L"GetGamingFanSpeed", kSpeedIndex[i], speed))
            return false;
        mode = FanMode((behavior >> kBehaviorReadShift[i]) & 0x3);
        percent = int((speed >> 8) & 0xFF);
        return true;
    }

    bool setFanMode(Fan fan, FanMode mode, int percent) override
    {
        const int i = int(fan);
        quint64 out;
        const quint64 behavior = kBehaviorBit[i] | (quint64(mode) << kBehaviorShift[i]);
        if (!call(L"SetGamingFanBehavior", behavior, out) || !statusOk(out))
            return false;
        if (mode != FanMode::Custom)
            return true;
        const quint64 speed = (quint64(qBound(0, percent, 100)) << 8) | kSpeedIndex[i];
        return call(L"SetGamingFanSpeed", speed, out) && statusOk(out);
    }

private:
    bool connect()
    {
        // Qt already initialises COM as STA on the GUI thread; S_FALSE still needs a matching CoUninitialize
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr))
            m_comInit = true;
        else if (hr != RPC_E_CHANGED_MODE) {
            m_error = "COM init failed: " + hrText(hr);
            return false;
        }
        // Fails with RPC_E_TOO_LATE if something already set it, which is fine
        CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                             nullptr, EOAC_NONE, nullptr);

        IWbemLocator *locator = nullptr;
        hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                              reinterpret_cast<void **>(&locator));
        if (FAILED(hr)) {
            m_error = "Cannot create WMI locator: " + hrText(hr);
            return false;
        }
        hr = locator->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &m_svc);
        locator->Release();
        if (FAILED(hr)) {
            m_error = "Cannot connect to ROOT\\WMI: " + hrText(hr);
            return false;
        }
        CoSetProxyBlanket(m_svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                          RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

        hr = m_svc->GetObject(_bstr_t(L"AcerGamingFunction"), 0, nullptr, &m_class, nullptr);
        if (FAILED(hr)) {
            m_error = "AcerGamingFunction not found (not an Acer gaming laptop?): " + hrText(hr);
            return false;
        }

        IEnumWbemClassObject *list = nullptr;
        hr = m_svc->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT * FROM AcerGamingFunction"),
                              WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &list);
        if (FAILED(hr)) {
            m_error = "Cannot query AcerGamingFunction: " + hrText(hr);
            return false;
        }
        IWbemClassObject *instance = nullptr;
        ULONG count = 0;
        hr = list->Next(WBEM_INFINITE, 1, &instance, &count);
        list->Release();
        if (FAILED(hr) || count == 0) {
            m_error = hr == HRESULT(WBEM_E_ACCESS_DENIED) ? QString("Access denied: run rawPSense as administrator")
                                                         : "No AcerGamingFunction instance: " + hrText(hr);
            return false;
        }
        VARIANT path;
        VariantInit(&path);
        instance->Get(L"__PATH", 0, &path, nullptr, nullptr);
        m_path = path.bstrVal;
        VariantClear(&path);
        instance->Release();
        return true;
    }

    // Invokes one AcerGamingFunction method with a single gmInput and returns gmOutput
    bool call(const wchar_t *method, quint64 input, quint64 &output)
    {
        if (!m_ok)
            return false;

        IWbemClassObject *signature = nullptr;
        HRESULT hr = m_class->GetMethod(method, 0, &signature, nullptr);
        if (FAILED(hr) || !signature) {
            m_error = QString("Method %1 not found").arg(QString::fromWCharArray(method));
            return false;
        }
        IWbemClassObject *in = nullptr;
        hr = signature->SpawnInstance(0, &in);
        signature->Release();
        if (FAILED(hr)) {
            m_error = "SpawnInstance failed: " + hrText(hr);
            return false;
        }

        // WMI passes uint64 values as decimal strings
        CIMTYPE type = CIM_UINT32;
        in->Get(L"gmInput", 0, nullptr, &type, nullptr);
        VARIANT arg;
        VariantInit(&arg);
        if (type == CIM_UINT64) {
            arg.vt = VT_BSTR;
            arg.bstrVal = SysAllocString(std::to_wstring(input).c_str());
        } else {
            arg.vt = VT_I4;
            arg.lVal = LONG(input);
        }
        hr = in->Put(L"gmInput", 0, &arg, 0);
        VariantClear(&arg);

        IWbemClassObject *out = nullptr;
        if (SUCCEEDED(hr))
            hr = m_svc->ExecMethod(m_path, _bstr_t(method), 0, nullptr, in, &out, nullptr);
        in->Release();
        if (FAILED(hr) || !out) {
            m_error = QString("%1 failed: %2").arg(QString::fromWCharArray(method), hrText(hr));
            return false;
        }

        VARIANT result;
        VariantInit(&result);
        out->Get(L"gmOutput", 0, &result, nullptr, nullptr);
        if (result.vt == VT_BSTR)
            output = _wcstoui64(result.bstrVal, nullptr, 10);
        else if (result.vt == VT_I4)
            output = quint32(result.lVal);
        else if (SUCCEEDED(VariantChangeType(&result, &result, 0, VT_UI8)))
            output = result.ullVal;
        else
            output = 0;
        VariantClear(&result);
        out->Release();
        return true;
    }

    bool statusOk(quint64 out)
    {
        if ((out & 0xFF) == 0)
            return true;
        m_error = QString("Firmware returned status 0x%1").arg(out & 0xFF, 0, 16);
        return false;
    }

    int sensor(Sensor id, quint64 mask)
    {
        quint64 out;
        if (!call(L"GetGamingSysInfo", 0x1 | (quint64(id) << 8), out) || (out & 0xFF) != 0)
            return -1;
        return int((out >> 8) & mask);
    }

    IWbemServices *m_svc = nullptr;
    IWbemClassObject *m_class = nullptr;
    _bstr_t m_path;
    bool m_comInit = false;
    bool m_ok = false;
};

} // namespace

std::unique_ptr<Backend> Backend::create()
{
    return std::make_unique<WindowsBackend>();
}
