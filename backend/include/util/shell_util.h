#pragma once

// Small, security-conservative helpers for running fixed shell commands and
// probing TCP reachability. Commands are always constant strings supplied by
// the caller; never interpolate request/user input into them.

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ibmcp::util {

struct ShellResult {
    int rc = -1;          // -1 means popen itself failed
    std::string output;   // combined stdout+stderr, trailing newlines trimmed
    bool ok() const { return rc == 0; }
};

// Run a fixed command through /bin/sh -c with stderr merged into stdout.
inline ShellResult shellExec(const std::string &cmd) {
    ShellResult r;
    std::array<char, 4096> buf{};
    FILE *fp = popen((cmd + " 2>&1").c_str(), "r");
    if (!fp) {
        r.output = std::string("popen failed: ") + std::strerror(errno);
        return r;
    }
    while (fgets(buf.data(), static_cast<int>(buf.size()), fp) != nullptr) {
        r.output.append(buf.data());
    }
    int status = pclose(fp);
    if (WIFEXITED(status)) r.rc = WEXITSTATUS(status);
    while (!r.output.empty() && (r.output.back() == '\n' || r.output.back() == '\r')) {
        r.output.pop_back();
    }
    return r;
}

// Non-blocking TCP connect to host:port with a millisecond timeout.
inline bool tcpReachable(const std::string &host, int port, int timeout_ms = 1200) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        ::close(fd);
        return false;
    }
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    bool reachable = false;
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1) {
        int rc = ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        if (rc == 0) {
            reachable = true;
        } else if (errno == EINPROGRESS) {
            fd_set wset;
            FD_ZERO(&wset);
            FD_SET(fd, &wset);
            timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            int s = ::select(fd + 1, nullptr, &wset, nullptr, &tv);
            if (s > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                    reachable = true;
                }
            }
        }
    }
    ::close(fd);
    return reachable;
}

}  // namespace ibmcp::util
