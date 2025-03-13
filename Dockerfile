FROM centos:8

# 替换为 CentOS Vault 镜像源
RUN sed -i 's|mirrorlist=|#mirrorlist=|g' /etc/yum.repos.d/CentOS-* && \
    sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-*

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