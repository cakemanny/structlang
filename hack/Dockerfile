FROM debian:bookworm-slim

RUN apt update && \
    apt install -y \
        clang \
        make \
        flex \
        bison \
        rustc

WORKDIR /app

ENV CC=clang

CMD ["bash"]
