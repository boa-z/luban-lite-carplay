/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "mbedtls/bignum.h"

typedef char limb_width_must_match[
    sizeof(mbedtls_mpi_uint) * 8U == EXPECT_LIMB_BITS ? 1 : -1];

int main(void)
{
    /* Upstream multiplication, division, modular exponentiation, inverse,
     * and GCD vectors exercise carry/borrow across limb boundaries. */
    assert(mbedtls_mpi_self_test(1) == 0);
    printf("MPI arithmetic passed: limb_bits=%u\n",
           (unsigned)(8U * sizeof(mbedtls_mpi_uint)));
    return 0;
}
