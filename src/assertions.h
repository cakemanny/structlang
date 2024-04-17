/*
 * A C convention is to define NDEBUG for release builds, which in turn
 * means that debug code, such as assertions, are turned into void statements.
 * We would like to continue the convention of defining NDEBUG for release
 * builds but would like to keep the assertions in many places.
 */

#ifdef NDEBUG
#  undef NDEBUG
#  include <assert.h>
#  define NDEBUG 1
#else
#  include <assert.h>
#endif // NDEBUG
