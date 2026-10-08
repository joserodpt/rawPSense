#pragma once

#include <QString>

// Starting rawPSense at sign-in, minimised to the tray (launched with --minimized).
// Windows uses a Task Scheduler logon task, since a Run registry entry can't start an app that
// needs administrator rights; Linux uses an XDG autostart entry.
namespace Autostart {

bool isEnabled();
bool setEnabled(bool on, QString &error);

} // namespace Autostart
