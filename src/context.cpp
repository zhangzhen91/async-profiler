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
    _pages.resize(_maxPages, nullptr);
}

unsigned int Context::pages(int tid) {
    return static_cast<unsigned int>(tid) >> 10;
}

unsigned int Context::maxPages() const {
    return this->_maxPages;
}

ThreadContext *Context::getThreadContext(int tid) {
    ContextPage *contextPage = getPage(tid);
    return &contextPage->slots[tid % 1024];
}


ContextPage *Context::getPage(int tid) {
    unsigned int pageIndex = pages(tid);
    if (pageIndex > this->_maxPages) {
        return nullptr;
    }
    ContextPage *contextPage = this->_pages[pageIndex];
    if (contextPage == nullptr) {
        contextPage = new ContextPage();
        this->_pages[pageIndex] = contextPage;
    }
    return contextPage;
}

// int main(){
//     // 调用 Contexts::getPage 获取页的起始地址
//     for (int i = 0; i < 6544320; ++i) {
//         ContextPage* pageAddress = Context::getInstance().getPage(i+1024);
//         if (!pageAddress) {
//             std::cout << "Info: This is a log message" << std::endl;
//             return 0; // 如果获取失败，返回 null
//         }
//     }
//     return 0;
// }