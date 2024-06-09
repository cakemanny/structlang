FROM debian:bookworm-slim

RUN --mount=type=cache,target=/var/cache/apt \
    apt update && \
    apt install -y \
        clang \
        make \
        flex \
        bison \
        man-db \
        gdb \
        lldb

WORKDIR /app

ENV CC=clang \
    ASAN_OPTIONS=detect_leaks=0

CMD ["bash"]
