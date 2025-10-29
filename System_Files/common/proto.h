#ifndef PROTO_H
#define PROTO_H

#include <string>
#include <vector>
#include <cstdint>
#include <sys/types.h>

// reliable send of exactly len bytes
ssize_t send_all(int fd, const void *buf, size_t len);
// reliable recv of exactly len bytes
ssize_t recv_all(int fd, void *buf, size_t len);

// length-prefixed message send/recv helpers
bool send_msg(int fd, const std::string &s);
bool recv_msg(int fd, std::string &out);

// split by ASCII whitespace into tokens
std::vector<std::string> split_ws(const std::string &s);

#endif