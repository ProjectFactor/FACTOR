/*
 * bench_factor_popen.c
 *
 * Benchmarks block solving using coreutils `factor` via popen (batch).
 * Simulates solving NBLOCKS blocks at nBits=32 (regtest difficulty).
 *
 * Build: gcc -O2 -o bench_factor_popen bench_factor_popen.c
 * Usage: poop ./bench_factor_rho ./bench_factor_popen
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBITS 32
#define NBLOCKS 200
#define TILDEN (16 * NBITS)  /* candidate search radius */

/* Maximum candidates per W: 2 per jj step (+ and - direction) */
#define MAX_CANDIDATES (TILDEN * 2)

/* Small-prime sieve: check divisibility by small primes individually.
 * We can't use a single product here since we don't have GMP. */
static const uint64_t SMALL_P[] = {3,5,7,11,13,17,19,23,29,31,37,41,43,47};
#define NSMALL (sizeof(SMALL_P) / sizeof(SMALL_P[0]))

static int coprime_to_small(uint64_t n)
{
    for (int i = 0; i < (int)NSMALL; i++) {
        if (n % SMALL_P[i] == 0)
            return 0;
    }
    return 1;
}

/*
 * Factor candidates for a single W using one batched `factor` call.
 * Returns 1 if a valid semiprime was found.
 */
static int solve_one(uint64_t W, uint64_t *out_factor, int64_t *out_offset)
{
    uint64_t one = (W & 1) ? 0 : 1;

    /* Collect sieve-surviving candidates with their offsets */
    uint64_t cands[MAX_CANDIDATES];
    int64_t  offsets[MAX_CANDIDATES];
    int ncands = 0;

    for (int jj = 0; jj < TILDEN; jj += 2) {
        uint64_t N1 = W + jj + one;
        uint64_t N2 = W - jj - one;

        if (coprime_to_small(N1)) {
            cands[ncands] = N1;
            offsets[ncands] = jj + (int64_t)one;
            ncands++;
        }
        if (coprime_to_small(N2)) {
            cands[ncands] = N2;
            offsets[ncands] = -jj - (int64_t)one;
            ncands++;
        }
    }

    if (ncands == 0)
        return 0;

    /* Build the command: "factor N1 N2 N3 ..." */
    /* Each number is at most 10 digits + space, plus "factor " prefix */
    size_t cmd_cap = 8 + (size_t)ncands * 12;
    char *cmd = malloc(cmd_cap);
    if (!cmd) return 0;

    int pos = snprintf(cmd, cmd_cap, "factor");
    for (int i = 0; i < ncands; i++) {
        pos += snprintf(cmd + pos, cmd_cap - pos, " %lu", (unsigned long)cands[i]);
    }

    FILE *fp = popen(cmd, "r");
    free(cmd);
    if (!fp) return 0;

    /* Parse output line by line: "N: f1 f2 f3 ..." */
    char line[1024];
    int found = 0;

    while (fgets(line, sizeof(line), fp) && !found) {
        /* Parse "N: f1 f2 ..." */
        char *colon = strchr(line, ':');
        if (!colon) continue;

        uint64_t N = strtoull(line, NULL, 10);
        char *rest = colon + 1;

        /* Collect distinct prime factors and their total count */
        uint64_t factors[64];
        int nfactors = 0;
        int total_factors = 0;
        char *tok = strtok(rest, " \t\n");
        while (tok && nfactors < 64) {
            uint64_t f = strtoull(tok, NULL, 10);
            total_factors++;
            /* Only add distinct factors */
            if (nfactors == 0 || factors[nfactors - 1] != f) {
                factors[nfactors++] = f;
            }
            tok = strtok(NULL, " \t\n");
        }

        /* A semiprime is p*q (exactly 2 prime factors total, both distinct) */
        if (total_factors != 2 || nfactors != 2) continue;

        /* factors[] is already sorted (factor outputs in ascending order) */

        /* Smaller factor must have exactly expected bits (matches consensus) */
        int b0 = 64 - __builtin_clzll(factors[0]);
        int expected = NBITS / 2 + (NBITS & 1);

        if (b0 != expected) continue;

        /* Edge case: factor must not be a power of 2 */
        if (factors[0] == (1ULL << (NBITS/2 - 1))) continue;

        /* Find matching offset */
        for (int i = 0; i < ncands; i++) {
            if (cands[i] == N) {
                *out_factor = factors[0];
                *out_offset = offsets[i];
                found = 1;
                break;
            }
        }
    }

    pclose(fp);
    return found;
}

int main(void)
{
    /* Same PRNG and seed as rho benchmark for identical W values */
    uint64_t state = 0xDEADBEEFCAFEBABEULL;
    int solved = 0;

    for (int i = 0; i < NBLOCKS; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;

        uint64_t W = state;
        W |= (1ULL << (NBITS - 1));
        W &= (1ULL << NBITS) - 1;
        W |= 1;

        if (i < 5)
            printf("W[%d] = %lu\n", i, (unsigned long)W);

        uint64_t factor;
        int64_t offset;
        if (solve_one(W, &factor, &offset))
            solved++;
    }

    printf("popen: solved %d/%d blocks\n", solved, NBLOCKS);
    return 0;
}
