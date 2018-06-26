# mtrace compile environment
FROM debian:jessie
MAINTAINER Wenxuan Yang "ywx217@gmail.com"
RUN apt-get update -y \
    && apt-get install -y --no-install-recommends \
        build-essential \
        'g++' \
        git \
        liblzma-dev \
        libssl-dev \
        libcrypto++-dev \
        python2.7 \
        python2.7-dev \
        autoconf \
        automake \
        cmake \
        python-pip \
        libtool \
        libbz2-dev \
        libboost-dev \
        libboost-date-time-dev \
        libboost-filesystem-dev \
        libboost-system-dev \
        rsync \
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
    && make && make install \
    && cd .. && rm -rf libunwind-1.1

# build libcurl for static linking
RUN git clone https://github.com/curl/curl.git -b curl-7_60_0 \
    && cd curl && mkdir build && cd build \
    && cmake -DCURL_STATIC_LIB=ON -DHTTP_ONLY=ON -DCMAKE_USE_LIBSSH2=OFF .. \
    && make \
    && cp lib/libcurl.a /usr/lib/x86_64-linux-gnu/libcurl.a \
    && cd ../../ && rm -rf curl
