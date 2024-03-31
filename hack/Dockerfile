FROM debian:bookworm-slim

RUN apt update && \
    apt install -y \
        clang \
        make \
        flex \
        bison \
        rustc

RUN apt update && \
    apt install -y \
        man-db \
        gdb \
        lldb

WORKDIR /app

ENV CC=clang \
    ASAN_OPTIONS=detect_leaks=0

CMD ["bash"]
