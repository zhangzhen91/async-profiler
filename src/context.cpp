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
    _maxPages = pages(OS::getMaxThreadId());
    _pages.resize(_maxPages, nullptr);
}

unsigned int Context::pages(int tid) {
    return (unsigned int) (tid + 1023) >> 10;
}

unsigned int Context::maxPages() {
    return this->_maxPages;
}

ThreadContext *Context::getThreadContext(int tid) {
    ContextPage *contextPage = getPage(tid);
    return &contextPage->slots[tid % 1024];
}


ContextPage *Context::getPage(int tid) {
    unsigned int pageIndex = pages(tid);
    ContextPage *contextPage = this->_pages[pageIndex];
    if (contextPage == nullptr) {
        contextPage = new ContextPage();
        this->_pages[pageIndex] = contextPage;
    }
    // No need to check if threadContext is nullptr since slots are objects
    return contextPage;
}

//int main(){
//    // 调用 Contexts::getPage 获取页的起始地址
//    int tid = 12099;
//    ContextPage* pageAddress = Context::getInstance().getPage(tid);
//    if (!pageAddress) {
//        return 0; // 如果获取失败，返回 null
//    }
//    // 创建一个直接字节缓冲区
//    std::cout << sizeof(pageAddress->slots) << std::endl;
//
//    return 0;
//}