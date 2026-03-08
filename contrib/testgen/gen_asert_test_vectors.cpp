// Copyright (c) 2020 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// This program generates a set of ASERT test vectors, grouped as "runs"
// which start from a given anchor block datum (height, nBits, timestamp
// of anchor block's parent).
//
// The generated test vectors give block heights, times and ASERT nBits
// calculated at that height (i.e. for the next block).
//
// The vectors are intended to validate implementations of the same
// integer ASERT algorithm in other languages against the C++ implementation.
//
// This program needs to be compiled and linked against pre-built BCHN
// libraries and sources. There should be some build instructions
// in the same folder as this program's source.

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <pow.h>
#include <util/system.h>

#include <cinttypes>
#include <cstdio>
#include <functional>
#include <random>

/** No-op translate strings, because having problems getting _() defined for linker */
const std::function<std::string(const char *)> G_TRANSLATION_FUN = [](const char *psz) -> std::string { return psz; };

// The vectors are produced in "runs" which iterate a number of blocks,
// based on some reference block parameters, a start height + increment,
// and a function which produces the time offset between iterated blocks.
// The following structure defines a run.
struct run_params {
    std::string run_name;                                     // a descriptive name for this run
    uint32_t anchor_nbits;                                    // real blocks only have target in form of nBits
    uint64_t anchor_height;                                   // height of anchor block (must be > 0)
    uint64_t anchor_time;                                     // timestamp of anchor block's parent
    uint64_t start_height;                                    // height at which to start calculating ASERT targets
    uint64_t start_time;                                      // time at start height
    uint64_t iterations;                                      // number of blocks to iterate
    uint64_t (*height_incr_function)(uint64_t prev_height);   // a height increment function which yields the time diff at an iteration
    int64_t (*timediff_function)(uint64_t iteration);         // a function which yields the time diff at an iteration
};

// Height & time diff producing functions
// These are used as function pointers in the run table
// to obtain desired run behaviors.

uint64_t height_incr_by_one(uint64_t) {
    return 1;
}

uint64_t height_incr_by_288(uint64_t) {
    return 288;
}

int64_t time_incr_by_600s(uint64_t) {
    return 600;
}

int64_t time_incr_by_extra_halflife(uint64_t) {
    // This adds another 600s because the extra halflife
    // is supposed to be relative to the height of each
    // block, and we are aiming for a nice doubling of
    // the target in the only run that uses this increment
    // function.
    return 600 + 2*24*3600;
}

// stable hashrate, solvetime is exponentially distributed
int64_t time_incr_by_random(uint64_t) {
    static std::mt19937 trng {424242};
    static std::exponential_distribution<double> dist{1/600.0};
    return int64_t(dist(trng));
}

// hashrate jumps up (doubles) every 10 blocks
int64_t time_incr_by_random_ramp_up(uint64_t iteration) {
    static std::mt19937 trng {424242};
    static std::exponential_distribution<double> dist{1/600.0};
    return int64_t(dist(trng)) / (1 + int64_t(iteration / 10));
}

// hashrate jumps down (halves) every 10 blocks
int64_t time_incr_by_random_ramp_down(uint64_t iteration) {
    static std::mt19937 trng {424242};
    static std::exponential_distribution<double> dist{1/600.0};
    return int64_t(dist(trng)) * (1 + int64_t(iteration / 10));
}

// stable hashrate, solvetime is uniformly distributed in [-300,900]
int64_t time_incr_by_uniform_random(uint64_t) {
    static std::mt19937 trng {424242};
    static std::uniform_int_distribution<int64_t> dist{-300, 900};
    return int64_t(dist(trng));
}

void print_run_iteration(const uint64_t iteration,
                         const uint64_t height,
                         const uint64_t time,
                         const uint32_t target_nbits) {
    std::printf("%" PRIu64 " %" PRIu64 " %" PRIu64 " 0x%08x\n", iteration, height, time, target_nbits);
}

// Perform one parameterized run to produce ASERT test vectors.
void perform_run(const run_params& r_params, const Consensus::Params& chainparams) {
    assert(r_params.start_height >= r_params.anchor_height);
    const arith_uint256 powLimit = UintToArith256(chainparams.powLimit);
    const arith_uint256 refTarget = arith_uint256().SetCompact(r_params.anchor_nbits);
    const uint32_t check_nbits = refTarget.GetCompact();
    assert(check_nbits == r_params.anchor_nbits);

    // Print run header info
    std::printf("## description: %s\n", r_params.run_name.data());
    std::printf("##   anchor height: %" PRIu64 "\n", r_params.anchor_height);
    std::printf("##   anchor parent time: %" PRIu64 "\n", r_params.anchor_time);
    std::printf("##   anchor nBits: 0x%08x\n", r_params.anchor_nbits);
    std::printf("##   start height: %" PRIu64 "\n", r_params.start_height);
    std::printf("##   start time: %" PRIu64 "\n", r_params.start_time);
    std::printf("##   iterations: %" PRIu64 "\n", r_params.iterations);
    std::printf("# iteration,height,time,target\n");

    for (uint64_t i=1, h=r_params.start_height, t=r_params.start_time; i <= r_params.iterations; ++i) {
        arith_uint256 nextTarget;
        uint32_t nextBits;

        nextTarget = CalculateASERT(refTarget,
                                    chainparams.nPowTargetSpacing,
                                    t - r_params.anchor_time,    // time diff to reference
                                    h - r_params.anchor_height,  // height diff to reference
                                    powLimit,
                                    chainparams.nASERTHalfLife);

        // Calculate the bits from target
        nextBits = nextTarget.GetCompact();

        // Print out the iteration values
        print_run_iteration(i, h, t, nextBits);

        // increment the height by whatever the height increment function
        // yields for current height
        h += (*r_params.height_incr_function)(h);

        // increment the time by whatever the time increment function
        // yields for current iteration.
        // Iterations are counted starting from 1 to keep things natural
        t += (*r_params.timediff_function)(i);
    }
}


// Produce test vectors for ASERT.
void produce_asert_test_vectors() {
    DummyConfig config(CBaseChainParams::MAIN);
    const Consensus::Params &chainparams = config.GetChainParams().GetConsensus();
    const arith_uint256 powLimit = UintToArith256(chainparams.powLimit);
    uint32_t powLimit_nBits = powLimit.GetCompact();

    // Table of runs used to produce the test vectors.
    const std::vector<run_params> run_table = {
        /* run_name,
         * anchor_nBits, anchor_height, anchor_time, start_height, start_time, iterations, height_incr_function, timediff_function */

        // example of usable lamda (not allowed to capture anything,
        // otherwise cannot be used as function pointer:
        //    [](uint64_t) { return int64_t(600+172800); }

        { "run1 - steady 600s blocks at POW limit target",
            powLimit_nBits, 1ULL, 0ULL, 2ULL, 1200ULL, 10ULL, height_incr_by_one, time_incr_by_600s
        },

        { "run2 - steady 600s blocks at arbitrary non-limit target 0x1a2b3c4d",
            0x1a2b3c4d, 1ULL, 0ULL, 2ULL, 1200ULL, 10ULL, height_incr_by_one, time_incr_by_600s
        },

        { "run3 - steady 600s blocks at minimum limit target 0x01010000",
            0x01010000, 1ULL, 0ULL, 2ULL, 1200ULL, 10ULL, height_incr_by_one, time_incr_by_600s
        },

        { "run4 - from minimum target, a series of halflife schedule jumps, doubling target at each block",
            0x01010000, 1ULL, 0ULL, 2ULL, 600 + static_cast<uint64_t>(time_incr_by_extra_halflife(0)), 256-31, height_incr_by_one, time_incr_by_extra_halflife
        },

        { "run5 - from POW limit, a series of halflife block height jumps w/o time increment, halving target at each block",
            powLimit_nBits, 1ULL, 0ULL, 2ULL, 0ULL, 256-31, height_incr_by_288,
                                                            [](uint64_t) { return int64_t(0); }
        },

        { "run6 - deterministically random solvetimes for stable hashrate around a recent real life nBits",
            0x1802aee8, 1ULL, 0ULL, 2ULL, 1200ULL, 1000ULL, height_incr_by_one, time_incr_by_random
        },

        { "run7 - deterministically random solvetimes for up-ramping hashrate around a recent real life nBits",
            0x1802aee8, 1ULL, 0ULL, 2ULL, 1200ULL, 1000ULL, height_incr_by_one, time_incr_by_random_ramp_up
        },

        { "run8 - deterministically random solvetimes for down-ramping hashrate around a recent real life nBits",
            0x1802aee8, 1ULL, 0ULL, 2ULL, 1200ULL, 500ULL, height_incr_by_one, time_incr_by_random_ramp_down
        },

        { "run9 - a sequence of 300s blocks across signed 32-bit max integer height",
            0x1802aee8, 0x7FFFFFFFULL-5, 1234567890ULL-600ULL, 0x7FFFFFFFULL-4, 1234567890ULL+300, 10ULL, height_incr_by_one,
                                                                                                          [](uint64_t) { return int64_t(300); }
        },

        { "run10 - a sequence of 900s blocks across signed 64-bit max integer height and signed 32-bit max integer time ",
            0x1802aee8, 0x7FFFFFFFFFFFFFFFULL-5, 0x7FFFFFFFULL-600ULL, 0x7FFFFFFFFFFFFFFFULL-4, 0x7FFFFFFFULL+900, 10ULL, height_incr_by_one,
                                                                                                                          [](uint64_t) { return int64_t(900); }
        },

        { "run11 - deterministically uniform random solvetimes with negative time",
            0x1802aee8, 1ULL, 0ULL, 2ULL, 1200ULL, 1000ULL, height_incr_by_one, time_incr_by_uniform_random
        },

        { "run12 - each block 1 second before the previous one",
            0x1802aee8, 1ULL, 10000ULL, 2ULL, 11200ULL, 10000ULL, height_incr_by_one, [](uint64_t) { return int64_t(-1); }
        },


    };

    // Each run produces a bunch of test vectors.
    for (auto& r : run_table) {
        perform_run(r, chainparams);
        puts("");
    }
}


int main() {
    // Sanity-check the random number generator used to generate block time differences in some runs:
    // The 10000th invocation of mt19937 shall produce the value 4123659995.
    // using mt19937 = std::mersenne_twister_engine<uint_fast32_t,32,624,397,31,0x9908b0df,11,0xffffffff,7,0x9d2c5680,15,0xefc60000,18,1812433253>;

    std::mt19937 rng;
    std::mt19937::result_type rval{};
    for (int i = 0; i < 10'000; ++i)
        rval = rng();
    assert(rval == 4123659995UL);

    // calculate_asert_test_vectors();
    produce_asert_test_vectors();
}
