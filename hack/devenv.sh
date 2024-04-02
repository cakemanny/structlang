#!/bin/sh

set -eu

cd "$(dirname "$0")"

exec docker compose run -it --rm -u "$(id -u)" --build x86_64-linux
