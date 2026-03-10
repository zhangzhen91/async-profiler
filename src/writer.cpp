/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <limits>
#include <cstring>
#include "writer.h"


Writer& Writer::operator<<(char c) {
    write(&c, 1);
    return *this;
}

Writer& Writer::operator<<(const char* s) {
    if (s != nullptr) {
        write(s, strlen(s));
    }
    return *this;
}

Writer& Writer::operator<<(int n) {
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d", n);
    if (len > 0 && len < static_cast<int>(sizeof(buf))) {
        write(buf, len);
    }
    return *this;
}

Writer& Writer::operator<<(long n) {
    char buf[24];
    int len = snprintf(buf, sizeof(buf), "%ld", n);
    if (len > 0 && len < static_cast<int>(sizeof(buf))) {
        write(buf, len);
    }
    return *this;
}

Writer& Writer::operator<<(unsigned int n) {
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%u", n);
    if (len > 0 && len < static_cast<int>(sizeof(buf))) {
        write(buf, len);
    }
    return *this;
}

Writer& Writer::operator<<(unsigned long n) {
    char buf[24];
    int len = snprintf(buf, sizeof(buf), "%lu", n);
    if (len > 0 && len < static_cast<int>(sizeof(buf))) {
        write(buf, len);
    }
    return *this;
}

Writer& Writer::operator<<(long long n) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", n);
    if (len > 0 && len < static_cast<int>(sizeof(buf))) {
        write(buf, len);
    }
    return *this;
}

Writer& Writer::operator<<(unsigned long long n) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%llu", n);
    if (len > 0 && len < static_cast<int>(sizeof(buf))) {
        write(buf, len);
    }
    return *this;
}

Writer& Writer::operator<<(double d) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%.6g", d);
    if (len > 0 && len < static_cast<int>(sizeof(buf))) {
        write(buf, len);
    }
    return *this;
}

Writer& Writer::operator<<(float f) {
    return operator<<(static_cast<double>(f));
}

FileWriter::FileWriter(const char* file_name) : _fd(-1), _buf(nullptr), _size(0) {
    if (file_name != nullptr) {
        _fd = open(file_name, O_WRONLY | O_TRUNC | O_CREAT, 0644);
        if (_fd < 0) {
            _err = errno;
        }
    } else {
        _err = EINVAL;
    }
    _buf = static_cast<char*>(malloc(BUF_SIZE));
    if (_buf == nullptr) {
        _err = ENOMEM;
    }
}

FileWriter::FileWriter(int fd) : _fd(fd), _buf(nullptr), _size(0) {
    if (fd < 0) {
        _err = EINVAL;
    }
    _buf = static_cast<char*>(malloc(BUF_SIZE));
    if (_buf == nullptr) {
        _err = ENOMEM;
    }
}

FileWriter::~FileWriter() {
    if (_buf != nullptr) {
        flush(_buf, _size);
        free(_buf);
    }
    if (_fd >= 0 && _fd > STDERR_FILENO) {
        close(_fd);
    }
}

void FileWriter::flush(const char* data, size_t len) {
    if (data == nullptr || len == 0 || _fd < 0) {
        return;
    }
    
    while (len > 0) {
        ssize_t bytes = ::write(_fd, data, len);
        if (bytes <= 0) {
            if (bytes < 0) {
                _err = errno;
            }
            break;
        }
        data += bytes;
        len -= static_cast<size_t>(bytes);
    }
}

void FileWriter::write(const char* data, size_t len) {
    if (data == nullptr || len == 0 || _fd < 0) {
        return;
    }
    
    if (_size + len > BUF_SIZE) {
        flush(_buf, _size);
        _size = 0;
        if (len > BUF_SIZE) {
            flush(data, len);
            return;
        }
    }
    
    if (len <= BUF_SIZE - _size) {
        memcpy(_buf + _size, data, len);
        _size += len;
    }
}

BufferWriter::BufferWriter(size_t capacity) : _size(0), _capacity(capacity) {
    if (capacity == 0) {
        capacity = 256; // Default capacity
    }
    _buf = static_cast<char*>(malloc(capacity));
    if (_buf == nullptr) {
        _err = ENOMEM;
        _capacity = 0;
    }
}

BufferWriter::~BufferWriter() {
    if (_buf != nullptr) {
        free(_buf);
    }
}

void BufferWriter::write(const char* data, size_t len) {
    if (data == nullptr || len == 0) {
        return;
    }
    
    size_t new_size = _size + len;
    if (new_size > _capacity) {
        // Prevent integer overflow
        if (new_size < _size) {
            _err = EOVERFLOW;
            return;
        }
        
        size_t new_capacity = new_size >= _capacity * 2 ? new_size : _capacity * 2;
        // Check for overflow in capacity calculation
        if (new_capacity < _capacity) {
            new_capacity = new_size;
        }
        
        char* new_buf = static_cast<char*>(realloc(_buf, new_capacity));
        if (new_buf == nullptr) {
            _err = ENOMEM;
            return;
        }
        
        _buf = new_buf;
        _capacity = new_capacity;
    }
    
    memcpy(_buf + _size, data, len);
    _size = new_size;
}

void CallbackWriter::write(const char* data, size_t len) {
    if (data != nullptr && len > 0 && _output_callback != nullptr) {
        _output_callback(data, len);
    }
}

void LogWriter::write(const char* data, size_t len) {
    if (data != nullptr && len > 0) {
        Log::writeRaw(_logLevel, data, len);
    }
}
