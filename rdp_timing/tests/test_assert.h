/* Fail through stderr in unattended runs, including Windows console tests. */
#ifndef RDP_TIMING_TEST_ASSERT_H
#define RDP_TIMING_TEST_ASSERT_H
#include <stdio.h>
#include <stdlib.h>
static void port_check_fail(const char *condition, const char *file, unsigned line)
{
    fprintf(stderr, "%s:%u: check failed: %s\n", file, line, condition);
    exit(1);
}
#undef assert
#define assert(condition) ((condition) ? (void)0 : port_check_fail(#condition, __FILE__, __LINE__))
#endif
