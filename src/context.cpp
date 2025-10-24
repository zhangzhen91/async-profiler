//
// Created by zhangzhen62 on 2025/2/25.
//


#include "context.h"
#include "os.h"

Context &Context::getInstance() {
    static Context instance;
    return instance;
}

Context::Context() {
    _maxPages = pages(OS::getMaxThreadId()) + 1;
    _pages = new ContextPage *[_maxPages];
    // 初始化为 nullptr
    for (unsigned int i = 0; i < _maxPages; ++i) {
        _pages[i] = NULL;
    }
}

unsigned int Context::pages(int tid) {
    return static_cast<unsigned int>(tid) >> 10;
}

unsigned int Context::maxPages() const {
    return this->_maxPages;
}

ThreadContext *Context::getThreadContext(const int tid) const {
    ContextPage *contextPage = getPage(tid);
    if (contextPage == NULL) {
        return NULL;
    }
    return &contextPage->slots[tid % 1024];
}

ContextPage *Context::getPageOrCreate(const int tid) const {
    const unsigned int pageIndex = pages(tid);
    if (pageIndex >= this->_maxPages) {
        return NULL;
    }
    // 第一次尝试获取已存在的页面
    ContextPage *contextPage = __atomic_load_n(&this->_pages[pageIndex], __ATOMIC_ACQUIRE);
    if (contextPage != NULL) {
        return contextPage;
    }
    ContextPage *newPage = new ContextPage();
    ContextPage *expected = NULL;
    if (__atomic_compare_exchange_n(&this->_pages[pageIndex], &expected, newPage,
                                    false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return newPage;
    }
    delete newPage;
    return expected; // 返回其他线程创建的页面
}

ContextPage *Context::getPage(const int tid) const {
    const unsigned int pageIndex = pages(tid);
    if (pageIndex >= this->_maxPages) {
        return NULL;
    }
    return __atomic_load_n(&this->_pages[pageIndex], __ATOMIC_ACQUIRE);;
}

// int main(){
//     const int max_tid = OS::getMaxThreadId();
//     auto start = std::chrono::high_resolution_clock::now();
//     // 调用 Contexts::getPage 获取页的起始地址
//     int64_t trace_id = 0;
//     int64_t span_id = 0;
//     int64_t extend = 0;
//
//     for (int i = 0; i < 200000; ++i) {
//         const ThreadContext *threadContext = Context::getInstance().getThreadContext(i);
//         if (threadContext == nullptr) {
//             continue;
//         }
//         trace_id = threadContext->getTraceId();
//         span_id = threadContext->getSpanId();
//         extend = threadContext->getExtend();
//     }
//     std::cout << trace_id << std::endl;
//     std::cout << span_id << std::endl;
//     std::cout << extend << std::endl;
//     auto end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
//     std::cout << "执行时间: " << duration.count() << " ms" << std::endl;
//     return 0;
// }
