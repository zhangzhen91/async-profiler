//
// Created by zhangzhen62 on 2025/2/25.
//


#include "context.h"
#include "os.h"

Context& Context::getInstance() {
    static Context instance;
    return instance;
}

Context::Context() {
    _maxPages = pages(OS::getMaxThreadId());
    _pages.resize(_maxPages, nullptr);
}

unsigned int Context::pages(int tid) {
    return (unsigned int)(tid + 1023) >> 10;
}

unsigned int Context::maxPages() {
    return this->_maxPages;
}

ContextPage* Context::getPage(int tid) {
    unsigned int pageIndex = pages(tid);
    ContextPage* contextPage = this->_pages[pageIndex];
    if (contextPage == nullptr) {
        contextPage = new ContextPage();
        this->_pages[pageIndex] = contextPage;
    }
    // No need to check if threadContext is nullptr since slots are objects
    return contextPage;
}