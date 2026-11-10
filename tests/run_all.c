/* SPDX-License-Identifier: MPL-2.0 */
#include "jo_test.h"
int jt_fail, jt_pass;
#define X(n) void run_##n(void);
#include "suites.h"
#undef X
int main(void)
{
#define X(n) run_##n();
#include "suites.h"
#undef X
    printf("%d checks passed, %d failed\n", jt_pass, jt_fail);
    return jt_fail ? 1 : 0;
}
