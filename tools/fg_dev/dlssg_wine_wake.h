// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <cstdio>
#include <string>

// CPU directory notifications only. The caller still validates every control
// record and obtains GPU completion from its real fence, never from this event.
class DlssgWineWake {
    HANDLE notification = INVALID_HANDLE_VALUE;
    unsigned fallbacks = 0;
    void Fallback() noexcept {
        ++fallbacks;
        if (notification != INVALID_HANDLE_VALUE) {
            FindCloseChangeNotification(notification);
            notification = INVALID_HANDLE_VALUE;
        }
    }
public:
    DlssgWineWake() = default;
    DlssgWineWake(const DlssgWineWake&) = delete;
    DlssgWineWake& operator=(const DlssgWineWake&) = delete;
    ~DlssgWineWake() { if (notification != INVALID_HANDLE_VALUE) FindCloseChangeNotification(notification); }
    bool Open(const std::string& directory) {
        notification = FindFirstChangeNotificationA(directory.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
        if (notification == INVALID_HANDLE_VALUE) { ++fallbacks; return false; }
        return true;
    }
    bool Enabled() const { return notification != INVALID_HANDLE_VALUE; }
    unsigned Fallbacks() const { return fallbacks; }
    void Pause() {
        if (!Enabled()) { Sleep(1); return; }
        const DWORD result = WaitForSingleObject(notification, 50);
        if (result == WAIT_OBJECT_0) {
            // Rearm before the caller rescans the persistent record. A timeout
            // retries the same armed wait and must not call FindNext again.
            if (!FindNextChangeNotification(notification)) Fallback();
        } else if (result != WAIT_TIMEOUT) Fallback();
    }
};
