/*
 * bench_factor_rho.c
 *
 * Benchmarks block solving using in-process Pollard's rho (GMP).
 * Simulates solving NBLOCKS blocks at nBits=32 (regtest difficulty).
 *
 * Build: gcc -O2 -o bench_factor_rho bench_factor_rho.c -lgmp
 * Usage: poop ./bench_factor_rho ./bench_factor_popen
 */

#include <gmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define NBITS 32
#define NBLOCKS 200
#define TILDEN (16 * NBITS)  /* candidate search radius */

/* Small-prime sieve product: 3*5*7*11*13*17*19*23*29*31*37*41*43*47 */
static const uint64_t SMALL_PRIMES = 3ULL*5*7*11*13*17*19*23*29*31*37*41*43*47;

/* f(z) = z^2 + 1 mod n  (Pollard rho step) */
static void f(mpz_t z, const mpz_t n, const mpz_t two)
{
    mpz_powm(z, z, two, n);
    mpz_add_ui(z, z, 1ULL);
    mpz_mod(z, z, n);
}

/*
 * Pollard rho factoring.
 * Returns 1 if n = g * (n/g) with both factors prime.
 * Returns 0 if n is prime or factoring failed.
 */
static int rho(mpz_t g, mpz_t n)
{
    if (mpz_probab_prime_p(n, 25) != 0)
        return 0;

    mpz_t x, y, two, temp;
    mpz_init_set_ui(x, 2);
    mpz_init_set_ui(y, 2);
    mpz_init_set_ui(two, 2);
    mpz_set_ui(g, 1);
    mpz_init(temp);

    while (mpz_cmp_ui(g, 1) == 0) {
        f(x, n, two);
        f(y, n, two);
        f(y, n, two);

        mpz_sub(temp, x, y);
        mpz_gcd(g, temp, n);
    }

    mpz_divexact(temp, n, g);

    int u_p = mpz_probab_prime_p(temp, 30);
    int g_p = mpz_probab_prime_p(g, 30);

    mpz_clear(x);
    mpz_clear(y);
    mpz_clear(temp);
    mpz_clear(two);

    if ((u_p != 0) && (g_p != 0) && (mpz_cmp(g, n) != 0))
        return 1;

    return 0;
}

/*
 * Try to solve one "block" given W (a random nBits-bit number).
 * Returns 1 if a valid semiprime factorization was found, 0 otherwise.
 */
static int solve_one(uint64_t W, uint64_t *out_factor, int64_t *out_offset)
{
    mpz_t n, g, gcd_val;
    mpz_inits(n, g, gcd_val, NULL);

    uint64_t one = (W & 1) ? 0 : 1;
    int found = 0;

    for (int jj = 0; jj < TILDEN && !found; jj += 2) {
        uint64_t candidates[2] = { W + jj + one, W - jj - one };
        int64_t  offsets[2]    = { jj + (int64_t)one, -jj - (int64_t)one };

        for (int k = 0; k < 2 && !found; k++) {
            uint64_t N = candidates[k];
            mpz_set_ui(n, N);
            mpz_gcd_ui(gcd_val, n, SMALL_PRIMES);

            if (mpz_cmp_ui(gcd_val, 1) != 0)
                continue;

            int valid = rho(g, n);
            int bitsize = (int)mpz_sizeinbase(g, 2);

            if (valid == 1 && bitsize == (NBITS >> 1)) {
                uint64_t f1 = mpz_get_ui(g);
                uint64_t f2 = N / f1;
                uint64_t factor = (f1 < f2) ? f1 : f2;

                /* Edge case: cofactor must have same number of bits */
                int edge_check = f2 & (1ULL << (bitsize - 1));
                if (edge_check != 0 && factor != (1ULL << (NBITS/2 - 1))) {
                    *out_factor = factor;
                    *out_offset = offsets[k];
                    found = 1;
                }
            }
        }
    }

    mpz_clears(n, g, gcd_val, NULL);
    return found;
}

int main(void)
{
    /* Seed a simple PRNG (xorshift64) for reproducible W values */
    uint64_t state = 0xDEADBEEFCAFEBABEULL;
    int solved = 0;

    for (int i = 0; i < NBLOCKS; i++) {
        /* Generate a random nBits-bit W */
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;

        uint64_t W = state;
        /* Force exactly NBITS bits: set bit 31, clear bits above */
        W |= (1ULL << (NBITS - 1));
        W &= (1ULL << NBITS) - 1;
        /* Make sure it's odd (most semiprimes are) */
        W |= 1;

        if (i < 5)
            printf("W[%d] = %lu\n", i, (unsigned long)W);

        uint64_t factor;
        int64_t offset;
        if (solve_one(W, &factor, &offset))
            solved++;
    }

    printf("rho: solved %d/%d blocks\n", solved, NBLOCKS);
    return 0;
}
