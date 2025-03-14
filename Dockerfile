FROM centos:7


RUN sed -i 's/mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-*
RUN sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-*
# 安装 GCC 8.x
#RUN dnf install -y gcc-toolset-8
RUN yum install -y make

RUN yum install -y git
# 安装静态版本的 libstdc++
RUN yum install install -y gcc-c++

# 安装 JDK 11
RUN yum install install -y java-11-openjdk-devel

# 验证安装
RUN gcc --version && \
    java -version