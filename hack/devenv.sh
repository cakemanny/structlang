#!/bin/sh


exec docker compose run -it --rm -u "$(id -u)" --build x86_64-linux
