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
//     ContextPage* pageAddress = Context::getInstance().getPageOrCreate(0x7fffffff);
//     if (!pageAddress) {
//         return 0; // 如果获取失败，返回 null
//     }
//     return 0;
// }
