cmake_minimum_required(VERSION 3.30)
project(async_profiler)

set(CMAKE_CXX_STANDARD 14)


# 手动添加 JNI 包含路径（根据你的 JDK 路径调整）
set(JAVA_HOME "/Library/Java/JavaVirtualMachines/jdk-11.jdk/Contents/Home")
include_directories("${JAVA_HOME}/include")
include_directories("${JAVA_HOME}/include/darwin")  # macOS 专用

include_directories(./)
include_directories(src)
include_directories(src/jattach)
include_directories(src/main)
include_directories(src/helper)
include_directories(test/native)
include_directories(test/res)

# 编译定义 - 解决所有错误
add_compile_definitions(
        PROFILER_VERSION="1.0"  # 定义缺失的 PROFILER_VERSION
        _XOPEN_SOURCE           # 解决 ucontext.h 错误
        NO_ASM_RESOURCES  # 禁用内联汇编资源

)

add_executable(async_profiler
        src/context.cpp
        src/context.h
        src/os.h
        src/os_linux.cpp
        src/os_macos.cpp
        src/wallClock.cpp
        src/wallClock.h
        src/profiler.cpp
        src/profiler.cpp
    )
# 从文件列表中移除有问题的文件
list(FILTER SOURCES EXCLUDE REGEX ".*(flameGraph|flightRecorder|instrument|javaApi)\\.cpp$")