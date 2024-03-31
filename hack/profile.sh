#!/bin/sh

SCRIPT_DIR="$(dirname "$0")"
cd "$SCRIPT_DIR/.." || exit 1

make clean
make PROFILE=1

export LLVM_PROFILE_FILE=./build/llvm_%p.prof
#./tests/activation
#./build/debug/structlangc example.sl
./build/debug/structlangc tests/perf/many_funcs.sl >/dev/null

export PATH="/Library/Developer/CommandLineTools/usr/bin/:$PATH"
llvm-profdata merge -output=build/merge.out -instr build/llvm*.prof
llvm-profdata show --topn=20 build/merge.out
llvm-cov show ./build/debug/structlangc -instr-profile=build/merge.out --format=html > build/cov.html
open build/cov.html

sleep 10
rm build/cov.html ./build/*.prof build/merge.out
make clean
