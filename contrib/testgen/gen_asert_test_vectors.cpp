// Copyright (c) 2020 The Bitcoin developers
// Copyright (c) 2026 The FACTOR developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// This program generates FACTOR ASERT test vectors, grouped as "runs"
// which start from a given anchor block datum (height, nBits, timestamp).
//
// The generated test vectors give block heights, times and ASERT nBits
// calculated at that height (i.e. for the next block).
//
// The vectors are intended to validate implementations of the same
// FACTOR ASERT algorithm in other languages against the C++ implementation.
//
// This program needs to be compiled and linked against FACTOR's libraries.
// Build with: make -C src contrib/testgen/gen_asert_test_vectors

#include <pow.h>

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <functional>
#include <random>
#include <string>

// Required by the linker (referenced via util/translation.h)
const std::function<std::string(const char *)> G_TRANSLATION_FUN = nullptr;

// FACTOR ASERT parameters
static const FACTORASERTParams MAINNET_PARAMS = {
    1800,    // targetSpacing: 30 minutes
    172800,  // halfLife: 2 days
    32,      // nBitsMin
    1022     // nBitsMax
};

static const FACTORASERTParams TESTNET_PARAMS = {
    300,     // targetSpacing: 5 minutes
    3600,    // halfLife: 1 hour
    32,      // nBitsMin
    1022     // nBitsMax
};

// The vectors are produced in "runs" which iterate a number of blocks,
// based on a reference anchor, a start height + increment,
// and a function which produces the time offset between iterated blocks.
struct run_params {
    std::string run_name;
    const FACTORASERTParams *asert_params;
    int32_t anchor_nbits;                                     // anchor nBits (even, in [nBitsMin, nBitsMax])
    int64_t anchor_height;                                    // height of anchor block
    int64_t anchor_time;                                      // timestamp of anchor block
    int64_t start_height;                                     // height at which to start calculating
    int64_t start_time;                                       // time at start height
    uint64_t iterations;                                      // number of blocks to iterate
    uint64_t (*height_incr_function)(uint64_t prev_height);   // height increment function
    int64_t (*timediff_function)(uint64_t iteration);         // time diff function
};

// Height & time diff producing functions

uint64_t height_incr_by_one(uint64_t) {
    return 1;
}

int64_t time_incr_testnet_spacing(uint64_t) {
    return TESTNET_PARAMS.targetSpacing;  // 300s
}

int64_t time_incr_mainnet_spacing(uint64_t) {
    return MAINNET_PARAMS.targetSpacing;  // 1800s
}

// One halfLife behind schedule per block (testnet): difficulty should halve each step
int64_t time_incr_testnet_extra_halflife(uint64_t) {
    return TESTNET_PARAMS.targetSpacing + TESTNET_PARAMS.halfLife;
}

// Blocks arrive at half the target spacing (testnet): difficulty increases
int64_t time_incr_testnet_half_spacing(uint64_t) {
    return TESTNET_PARAMS.targetSpacing / 2;  // 150s
}

// Stable hashrate, solvetime is exponentially distributed (testnet)
int64_t time_incr_random_testnet(uint64_t) {
    static std::mt19937 trng{424242};
    static std::exponential_distribution<double> dist{1.0 / TESTNET_PARAMS.targetSpacing};
    return int64_t(dist(trng));
}

// Hashrate doubles every 10 blocks (testnet)
int64_t time_incr_random_ramp_up(uint64_t iteration) {
    static std::mt19937 trng{424242};
    static std::exponential_distribution<double> dist{1.0 / TESTNET_PARAMS.targetSpacing};
    return int64_t(dist(trng)) / (1 + int64_t(iteration / 10));
}

// Hashrate halves every 50 blocks (testnet)
int64_t time_incr_random_ramp_down(uint64_t iteration) {
    static std::mt19937 trng{424242};
    static std::exponential_distribution<double> dist{1.0 / TESTNET_PARAMS.targetSpacing};
    return int64_t(dist(trng)) * (1 + int64_t(iteration / 50));
}

// Uniform random in [-300, 900] (mean = target spacing = 300, includes negatives)
int64_t time_incr_uniform_random(uint64_t) {
    static std::mt19937 trng{424242};
    static std::uniform_int_distribution<int64_t> dist{
        -TESTNET_PARAMS.targetSpacing,
        3 * TESTNET_PARAMS.targetSpacing};
    return dist(trng);
}

void print_run_iteration(const uint64_t iteration,
                         const int64_t height,
                         const int64_t time,
                         const int32_t nbits,
                         const bool clamped_high) {
    std::printf("%" PRIu64 " %" PRId64 " %" PRId64 " %d %d\n",
                iteration, height, time, nbits, clamped_high ? 1 : 0);
}

// Perform one parameterized run to produce FACTOR ASERT test vectors.
// Takes by value: start_time is mutated for cumulative time tracking.
void perform_run(run_params r) {
    assert(r.start_height >= r.anchor_height);
    assert(r.anchor_nbits % 2 == 0);
    assert(r.anchor_nbits >= r.asert_params->nBitsMin);
    assert(r.anchor_nbits <= r.asert_params->nBitsMax);

    // Print run header
    std::printf("## description: %s\n", r.run_name.data());
    std::printf("##   anchor height: %" PRId64 "\n", r.anchor_height);
    std::printf("##   anchor time: %" PRId64 "\n", r.anchor_time);
    std::printf("##   anchor nBits: %d\n", r.anchor_nbits);
    std::printf("##   target spacing: %" PRId64 "\n", r.asert_params->targetSpacing);
    std::printf("##   half life: %" PRId64 "\n", r.asert_params->halfLife);
    std::printf("##   nBitsMin: %d  nBitsMax: %d\n", r.asert_params->nBitsMin, r.asert_params->nBitsMax);
    std::printf("##   start height: %" PRId64 "\n", r.start_height);
    std::printf("##   start time: %" PRId64 "\n", r.start_time);
    std::printf("##   iterations: %" PRIu64 "\n", r.iterations);
    std::printf("# iteration,height,time,nBits,clampedHigh\n");

    int64_t h = r.start_height;
    for (uint64_t i = 1; i <= r.iterations; ++i) {
        const int64_t timeDiff = r.start_time - r.anchor_time;
        const int64_t heightDiff = h - r.anchor_height;

        ASERTResult result = CalculateFACTORASERT(*r.asert_params, r.anchor_nbits, timeDiff, heightDiff);

        print_run_iteration(i, h, r.start_time, result.nBits, result.clampedHigh);

        h += static_cast<int64_t>((*r.height_incr_function)(h));
        r.start_time += (*r.timediff_function)(i);
    }
}

void produce_factor_asert_test_vectors() {
    const auto *tn = &TESTNET_PARAMS;
    const auto *mn = &MAINNET_PARAMS;

    const std::vector<run_params> run_table = {
        // --- Steady state ---
        {"run1 - testnet steady state at nBitsMin (32)",
            tn, 32, 1, 0, 2, 300, 10, height_incr_by_one, time_incr_testnet_spacing},

        {"run2 - testnet steady state at mid nBits (500)",
            tn, 500, 1, 0, 2, 300, 10, height_incr_by_one, time_incr_testnet_spacing},

        {"run3 - testnet steady state at nBitsMax (1022)",
            tn, 1022, 1, 0, 2, 300, 10, height_incr_by_one, time_incr_testnet_spacing},

        {"run4 - mainnet steady state at genesis (230)",
            mn, 230, 1, 0, 2, 1800, 10, height_incr_by_one, time_incr_mainnet_spacing},

        // --- Half-life behavior ---
        {"run5 - testnet: each block one halfLife behind schedule (difficulty halving each step)",
            tn, 500, 1, 0, 2, 300 + 3600, 20, height_incr_by_one, time_incr_testnet_extra_halflife},

        {"run6 - testnet: blocks at half spacing (difficulty increasing)",
            tn, 32, 1, 0, 2, 150, 50, height_incr_by_one, time_incr_testnet_half_spacing},

        // --- Random solvetimes ---
        {"run7 - testnet: random exponential solvetimes at mid difficulty",
            tn, 200, 1, 0, 2, 300, 1000, height_incr_by_one, time_incr_random_testnet},

        {"run8 - testnet: random solvetimes with ramping-up hashrate",
            tn, 200, 1, 0, 2, 300, 500, height_incr_by_one, time_incr_random_ramp_up},

        {"run9 - testnet: random solvetimes with ramping-down hashrate",
            tn, 200, 1, 0, 2, 300, 500, height_incr_by_one, time_incr_random_ramp_down},

        {"run10 - testnet: uniform random solvetimes (including negative)",
            tn, 200, 1, 0, 2, 300, 1000, height_incr_by_one, time_incr_uniform_random},

        // --- Boundary / clamp scenarios ---
        {"run11 - testnet: near nBitsMax with fast blocks (clampedHigh expected)",
            tn, 1000, 1, 0, 2, 1, 200, height_incr_by_one,
            [](uint64_t) { return int64_t(1); }},

        {"run12 - testnet: near nBitsMin with slow blocks (clampedLow expected)",
            tn, 34, 1, 0, 2, 600, 50, height_incr_by_one,
            [](uint64_t) { return int64_t(3600); }},

        // --- Negative time increments ---
        {"run13 - testnet: each block 1 second before the previous",
            tn, 200, 1, 10000, 2, 10300, 100, height_incr_by_one,
            [](uint64_t) { return int64_t(-1); }},
    };

    for (const auto& r : run_table) {
        perform_run(r);
        puts("");
    }
}


int main() {
    // Sanity-check the mt19937 RNG: 10000th value must be 4123659995.
    std::mt19937 rng;
    std::mt19937::result_type rval{};
    for (int i = 0; i < 10'000; ++i)
        rval = rng();
    assert(rval == 4123659995UL);

    produce_factor_asert_test_vectors();
}
