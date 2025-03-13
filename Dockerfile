FROM centos:8

# 安装 GCC 8.x
#RUN dnf install -y gcc-toolset-8
RUN yum install -y make

RUN yum install -y git
# 安装静态版本的 libstdc++
RUN dnf install -y gcc-c++

# 安装 JDK 11
RUN dnf install -y java-11-openjdk-devel

# 验证安装
RUN gcc --version && \
    java -version