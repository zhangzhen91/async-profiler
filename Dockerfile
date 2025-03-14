FROM centos:8


RUN sed -i 's/mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-*
RUN sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-*
# 安装 GCC 8.x
#RUN dnf install -y gcc-toolset-8

RUN yum install -y dnf-plugins-core
RUN yum config-manager --set-enabled powertools

RUN yum install -y epel-release

RUN yum install -y make

RUN yum install -y git
# 安装静态版本的 libstdc++
RUN yum install -y gcc-c++



RUN yum install -y libstdc++-static

RUN yum install -y patchelf
# 安装 JDK 11
RUN yum install -y java-11-openjdk-devel

# 验证安装
RUN gcc --version && \
    java -version