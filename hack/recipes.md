
## Testing for memory errors

AddressSanitizer no longer works for all our use cases with arena allocation.

On macOS, we can use libgmalloc

    make NASAN=1 clean all
    env DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib ./tests/activation


See libgmalloc(3) for more details


## Testings for memory leaks

On macOS:

    make NDEBUG=1
    MallocStackLogging=YES leaks -quiet -atExit -- ./build/release/structlangc -o /dev/null example.sl


On Linux:

    make clean all
    export ASAN_OPTIONS=detect_leaks=1
    ./build/debug/structlangc -o /dev/null example.sl
