/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _WRITER_H
#define _WRITER_H

#include "asprof.h"
#include "log.h"
#include <cstddef>
#include <cstdint>

// Forward declarations
class FileWriter;
class LogWriter;
class BufferWriter;
class CallbackWriter;

/**
 * Abstract base class for all writers.
 * Provides a common interface for writing data to various destinations.
 */
class Writer {
  protected:
    int _err = 0;

  public:
    Writer() = default;
    virtual ~Writer() = default;

    // Delete copy constructor and assignment operator
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    
    // Allow move constructor and assignment operator
    Writer(Writer&&) = default;
    Writer& operator=(Writer&&) = default;

    // Stream operators for various data types
    Writer& operator<<(char c);
    Writer& operator<<(const char* s);
    Writer& operator<<(int n);
    Writer& operator<<(long n);
    Writer& operator<<(unsigned int n);
    Writer& operator<<(unsigned long n);
    Writer& operator<<(long long n);
    Writer& operator<<(unsigned long long n);
    Writer& operator<<(float f);
    Writer& operator<<(double d);

    // Status checking
    bool good() const noexcept { return _err == 0; }
    bool bad() const noexcept { return _err != 0; }
    int error() const noexcept { return _err; }
    void clear_error() noexcept { _err = 0; }

    // Core writing interface
    virtual void write(const char* data, size_t len) = 0;
    
    // Convenience methods
    virtual void flush() {}  // Default no-op implementation
};

/**
 * File writer that writes data to a file descriptor.
 * Provides buffered writing for improved performance.
 */
class FileWriter : public Writer {
  private:
    int _fd = -1;
    char* _buf = nullptr;
    size_t _size = 0;

    static constexpr size_t BUF_SIZE = 8192;

    void flush(const char* data, size_t len);
    void flush_buffer();

  public:
    explicit FileWriter(const char* file_name);
    explicit FileWriter(int fd);
    ~FileWriter() noexcept;

    // Disable copy operations
    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    
    // Enable move operations
    FileWriter(FileWriter&& other) noexcept;
    FileWriter& operator=(FileWriter&& other) noexcept;

    bool is_open() const noexcept { return _fd >= 0; }
    int fd() const noexcept { return _fd; }
    
    // Override flush to actually flush the buffer
    void flush() override;
    
    virtual void write(const char* data, size_t len) override;
};

class LogWriter : public Writer {
    LogLevel _logLevel;

  public:
    LogWriter(LogLevel logLevel = LOG_INFO) : _logLevel(logLevel) {
    }

    virtual void write(const char* data, size_t len);
};

/**
 * Buffer writer that accumulates data in memory.
 * Provides dynamic buffer resizing and efficient memory management.
 */
class BufferWriter : public Writer {
  private:
    char* _buf = nullptr;
    size_t _size = 0;
    size_t _capacity = 0;

  public:
    explicit BufferWriter(size_t capacity = 256);
    ~BufferWriter() noexcept;

    // Disable copy operations
    BufferWriter(const BufferWriter&) = delete;
    BufferWriter& operator=(const BufferWriter&) = delete;
    
    // Enable move operations
    BufferWriter(BufferWriter&& other) noexcept;
    BufferWriter& operator=(BufferWriter&& other) noexcept;

    // Buffer access
    const char* data() const noexcept { return _buf; }
    char* data() noexcept { return _buf; }
    size_t size() const noexcept { return _size; }
    size_t capacity() const noexcept { return _capacity; }
    bool empty() const noexcept { return _size == 0; }
    
    // Buffer management
    void clear() noexcept { _size = 0; }
    bool reserve(size_t new_capacity);
    bool resize(size_t new_size);
    
    // String operations
    std::string str() const { return std::string(_buf, _size); }
    void assign(const char* data, size_t len);
    
    // Memory efficiency
    void shrink_to_fit();
    
    // Compatibility methods
    [[deprecated("Use data() instead")]]
    char* buf() const { return _buf; }
    
    virtual void write(const char* data, size_t len) override;
};

/**
 * Log writer that writes data to the logging system.
 */
class LogWriter : public Writer {
  private:
    LogLevel _logLevel;

  public:
    explicit LogWriter(LogLevel logLevel = LOG_INFO) : _logLevel(logLevel) {}
    
    void set_log_level(LogLevel level) noexcept { _logLevel = level; }
    LogLevel get_log_level() const noexcept { return _logLevel; }

    virtual void write(const char* data, size_t len) override;
};

/**
 * Callback writer that delegates writing to a user-provided callback function.
 */
class CallbackWriter : public Writer {
  private:
    asprof_writer_t _output_callback = nullptr;

  public:
    explicit CallbackWriter(asprof_writer_t output_callback) : _output_callback(output_callback) {}
    
    // Disable copy operations
    CallbackWriter(const CallbackWriter&) = delete;
    CallbackWriter& operator=(const CallbackWriter&) = delete;
    
    // Enable move operations
    CallbackWriter(CallbackWriter&& other) noexcept;
    CallbackWriter& operator=(CallbackWriter&& other) noexcept;
    
    void set_callback(asprof_writer_t callback) noexcept { _output_callback = callback; }
    asprof_writer_t get_callback() const noexcept { return _output_callback; }

    virtual void write(const char* data, size_t len) override;
};

// Utility functions for common writing patterns
namespace WriterUtils {
    /**
     * Create a formatted string and write it to the writer.
     * Similar to printf but writes to a Writer.
     */
    template<typename... Args>
    bool writef(Writer& writer, const char* format, Args... args);
    
    /**
     * Write a line of text followed by a newline character.
     */
    inline bool writeln(Writer& writer, const char* str) {
        if (str != nullptr) {
            writer << str << '\n';
            return writer.good();
        }
        return false;
    }
    
    /**
     * Write a formatted line of text.
     */
    template<typename... Args>
    bool writelnf(Writer& writer, const char* format, Args... args);
}

#endif // _WRITER_H
