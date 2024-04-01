#ifndef __TEST_HARNESS_H__
#define __TEST_HARNESS_H__
// vim:ft=c:

typedef void (*tsh_test_func_t)();

extern void tsh_register_test(
        tsh_test_func_t test_func,
        const char* file,
        const char* name);

#define REGISTER_TEST(test_func) \
    tsh_register_test(test_func, __FILE__, #test_func)


#endif /* __TEST_HARNESS_H__ */
