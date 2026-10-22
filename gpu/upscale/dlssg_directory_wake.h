// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>

namespace DlssgGpu {
// Notifications only prompt a JSON rescan; they never prove GPU completion or identity.
class DirectoryWake {
    int fd = -1;
public:
    DirectoryWake() = default;
    DirectoryWake(const DirectoryWake&) = delete;
    DirectoryWake& operator=(const DirectoryWake&) = delete;
    ~DirectoryWake() { if (fd >= 0) close(fd); }
    bool Open(const std::string& directory) {
        if (fd >= 0) throw std::runtime_error("GPU directory wake opened twice");
        fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (fd < 0) return false;
        // Watch the directory: atomic rename replaces the response inode each frame.
        if (inotify_add_watch(fd, directory.c_str(), IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF) < 0) {
            close(fd); fd = -1; return false;
        }
        return true;
    }
    bool Pause(int milliseconds = 50) const {
        if (fd < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); return false; }
        pollfd descriptor{fd, POLLIN, 0};
        const int result = poll(&descriptor, 1, milliseconds);
        if (result < 0 && errno == EINTR) return false;
        if (result < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
            throw std::runtime_error("GPU directory wake poll failed");
        if (!result) return false; // Periodic liveness and deadline check, not admission.
        // Never drain before poll: a rename between the caller's scan and this wait
        // must wake that scan immediately. Overflow is also just a rescan hint.
        alignas(inotify_event) std::array<char, 8192> buffer{};
        while (true) {
            const auto count = read(fd, buffer.data(), buffer.size());
            if (count > 0) continue;
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && errno == EAGAIN) break;
            throw std::runtime_error("GPU directory wake read failed");
        }
        return true;
    }
};
} // namespace DlssgGpu
