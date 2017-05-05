# mtrace compile environment
FROM debian:jessie
MAINTAINER Wenxuan Yang "gzyangwenxuan@corp.netease.com"
RUN apt-get update -y \
    && apt-get install -y \
        build-essential \
        'g++' \
        git \
        liblzma-dev \
        libssl-dev \
        libcrypto++-dev \
        python2.7 \
        python2.7-dev \
        autoconf \
        python-pip \
        libtool \
        && pip install Cython

# build patched libunwind
COPY libunwind-1.1.tar /root/
COPY libunwind-patch /root/libunwind-patch
RUN cd /root/ \
    && tar -xf libunwind-1.1.tar \
    && cd libunwind-1.1/ \
    && ((cd ../libunwind-patch && find . -type f) | xargs -I {} mv "../libunwind-patch/{}" "./{}") \
    && find ../libunwind-patch -type f \
    && autoreconf -i \
    && ./configure \
    && make
RUN cd /root/libunwind-1.1/ && make install
