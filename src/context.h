//
// Created by zhangzhen62 on 2025/2/25.
//

#ifndef CONTEXT_H
#define CONTEXT_H

#include <cstdint>
#include <iostream>

struct ThreadContext {
    int64_t trace_id;
    int64_t span_id;
    int64_t extend;

};

struct ContextPage {
    ThreadContext slots[1024];
};

class Context {
public:
    static Context& getInstance();
    unsigned int maxPages();
    ContextPage* getPage(int tid);
    ThreadContext* getThreadContext(int tid);


private:
    Context();
    unsigned int pages(int tid);

    std::vector<ContextPage*> _pages;
    unsigned int _maxPages;
};

#endif // CONTEXT_H