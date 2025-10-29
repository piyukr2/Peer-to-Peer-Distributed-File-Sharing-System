#include "proto.h"
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <cstring>
#include <sstream>

ssize_t send_all(int fd, const void *buf, size_t len) {
    const uint8_t *cursor = (const uint8_t*)buf;
    size_t remaining = len;
    while(remaining > 0) {
        ssize_t wrote = send(fd, cursor, remaining, 0);
        if(wrote <= 0) {
            if(errno == EINTR) continue;
            return -1;
        }
        cursor += wrote;
        remaining -= (size_t)wrote;
    }
    return (ssize_t)len;
}

ssize_t recv_all(int fd, void *buf, size_t len) {
    uint8_t *cursor = (uint8_t*)buf;
    size_t remaining = len;
    while(remaining > 0) {
        ssize_t readn = recv(fd, cursor, remaining, 0);
        if(readn <= 0) {
            if(errno == EINTR) continue;
            return -1;
        }
        cursor += readn;
        remaining -= (size_t)readn;
    }
    return (ssize_t)len;
}

bool send_msg(int fd, const std::string &s) {
    uint32_t n = htonl((uint32_t)s.size());
    if(send_all(fd, &n, 4) != 4) return false;
    if(s.empty()) return true;
    return send_all(fd, s.data(), s.size()) == (ssize_t)s.size();
}

bool recv_msg(int fd, std::string &out) {
    uint32_t n;
    if(recv_all(fd, &n, 4) != 4) return false;
    n = ntohl(n);
    if(n == 0) { out.clear(); return true; }
    if(n > 2u*1024u*1024u) return false; // cap messages at 2MB
    out.resize(n);
    return recv_all(fd, &out[0], n) == (ssize_t)n;
}

std::vector<std::string> split_ws(const std::string &s) {
    std::istringstream ss(s);
    std::vector<std::string> tokens;
    std::string token;
    while(ss >> token) tokens.push_back(token);
    return tokens;
}