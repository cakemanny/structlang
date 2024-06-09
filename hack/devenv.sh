#!/bin/sh

set -eu

cd "$(dirname "$0")"

arch=${1:-x86_64}
case $arch in
    "x86_64"|"aarch64")
        ;;
    *)
        echo "unknown arch: $1" >&2
        exit 1
        ;;
esac

exec docker compose run -it --rm -u "$(id -u)" --build "$arch-linux"
