FROM ubuntu:16.04
MAINTAINER wookhyunhan@gmail.com
ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get install -y clang cmake make ninja-build && \
    apt-get autoremove -y && \
    apt-get clean

ENV SRC=/src
RUN mkdir -p $SRC
COPY . $SRC
WORKDIR $SRC
RUN make
ENV PATH="$SRC/build/bin:$PATH"
