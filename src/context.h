//
// Created by zhangzhen62 on 2025/2/25.
//

#ifndef CONTEXT_H
#define CONTEXT_H

#include <cstdint>
#include <iostream>

struct ThreadContext {
private:
    int64_t trace_id;
    int64_t span_id;
    int64_t extend;
public:
    ThreadContext() : trace_id(0), span_id(0), extend(0) {}
    // Getter 方法
    int64_t getTraceId() const {
        return __atomic_load_n(&trace_id, __ATOMIC_ACQUIRE);
    }
    int64_t getSpanId()  const {
        return __atomic_load_n(&span_id, __ATOMIC_ACQUIRE);
    }
    int64_t getExtend()  const {
        return __atomic_load_n(&extend, __ATOMIC_ACQUIRE);
    }
};

struct ContextPage {
    ThreadContext slots[1024];
};

class Context {
public:
    static Context& getInstance();

    /**
     *  获取最大页数
     * @return
     */
    unsigned int maxPages() const;
    /**
     *  获取指定tid的页
     * @return
     */
    ContextPage *getPage(int tid) const;
    /**
     *  获取指定tid的页 如果不存在 则创建并返回
     * @return
     */
    ContextPage* getPageOrCreate(int tid) const;

    /**
     *  获取指定tid的线程对象
     * @param tid
     * @return
     */
    ThreadContext *getThreadContext(int tid) const;

    /**
     *  折构函数 释放内存
     */
    ~Context() {
        for (unsigned int i = 0; i < _maxPages; ++i) {
            delete _pages[i];
        }
        delete[] _pages;
    }

private:

    Context();

    static unsigned int pages(int tid);

    ContextPage**_pages;

    unsigned int _maxPages;
};

#endif // CONTEXT_H