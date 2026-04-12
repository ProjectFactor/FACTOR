// Copyright (c) 2026 The FACTOR developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>
#include <asert_table.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstdlib>

static const FACTORASERTParams MAINNET_PARAMS = {
    1800,    // targetSpacing: 30 minutes
    172800,  // halfLife: 2 days
    2,       // nBitsMin
    1022     // nBitsMax
};

static const FACTORASERTParams TESTNET_PARAMS = {
    300,     // targetSpacing: 5 minutes
    3600,    // halfLife: 1 hour
    2,       // nBitsMin
    1022     // nBitsMax
};

BOOST_AUTO_TEST_SUITE(asert_tests)

// When blocks arrive exactly on schedule, output must equal anchor.
BOOST_AUTO_TEST_CASE(steady_state) {
    const int32_t anchors[] = {2, 32, 100, 230, 394, 500, 1022};
    for (int32_t anchor : anchors) {
        // heightDiff=1, timeDiff=targetSpacing → on schedule
        ASERTResult r = CalculateFACTORASERT(MAINNET_PARAMS, anchor, 1800, 1);
        BOOST_CHECK_EQUAL(r.nBits, anchor);
        BOOST_CHECK_EQUAL(r.clampedHigh, false);

        // heightDiff=100, timeDiff=100*targetSpacing → still on schedule
        r = CalculateFACTORASERT(MAINNET_PARAMS, anchor, 100 * 1800, 100);
        BOOST_CHECK_EQUAL(r.nBits, anchor);
        BOOST_CHECK_EQUAL(r.clampedHigh, false);

        // Same with testnet params
        r = CalculateFACTORASERT(TESTNET_PARAMS, anchor, 300, 1);
        BOOST_CHECK_EQUAL(r.nBits, anchor);
        BOOST_CHECK_EQUAL(r.clampedHigh, false);
    }
}

// Blocks one halfLife behind schedule → difficulty should decrease (lower nBits).
BOOST_AUTO_TEST_CASE(halflife_easier) {
    // timeDiff = expected + halfLife → chain is slow → make easier
    // time_error = expected - timeDiff = -halfLife → exponent = -1.0 in Q32.32
    const int32_t anchor = 500;

    // Mainnet: heightDiff=100, timeDiff = 100*1800 + 172800
    ASERTResult r = CalculateFACTORASERT(MAINNET_PARAMS, anchor,
                                          100LL * 1800 + 172800, 100);
    BOOST_CHECK_LT(r.nBits, anchor);
    BOOST_CHECK_EQUAL(r.nBits % 2, 0);
    BOOST_CHECK_EQUAL(r.clampedHigh, false);

    // Testnet: heightDiff=100, timeDiff = 100*300 + 3600
    r = CalculateFACTORASERT(TESTNET_PARAMS, anchor,
                              100LL * 300 + 3600, 100);
    BOOST_CHECK_LT(r.nBits, anchor);
    BOOST_CHECK_EQUAL(r.nBits % 2, 0);
    BOOST_CHECK_EQUAL(r.clampedHigh, false);
}

// Blocks one halfLife ahead of schedule → difficulty should increase (higher nBits).
BOOST_AUTO_TEST_CASE(halflife_harder) {
    // timeDiff = expected - halfLife → chain is fast → make harder
    // time_error = expected - timeDiff = +halfLife → exponent = +1.0 in Q32.32
    const int32_t anchor = 500;

    // Mainnet: heightDiff=100, timeDiff = 100*1800 - 172800
    ASERTResult r = CalculateFACTORASERT(MAINNET_PARAMS, anchor,
                                          100LL * 1800 - 172800, 100);
    BOOST_CHECK_GT(r.nBits, anchor);
    BOOST_CHECK_EQUAL(r.nBits % 2, 0);
    BOOST_CHECK_EQUAL(r.clampedHigh, false);

    // Testnet
    r = CalculateFACTORASERT(TESTNET_PARAMS, anchor,
                              100LL * 300 - 3600, 100);
    BOOST_CHECK_GT(r.nBits, anchor);
    BOOST_CHECK_EQUAL(r.nBits % 2, 0);
    BOOST_CHECK_EQUAL(r.clampedHigh, false);
}

// ASERT(ASERT(n, +δ).nBits, -δ) should equal n for small δ in the table interior.
BOOST_AUTO_TEST_CASE(round_trip) {
    // Use testnet params with short halfLife for smaller deltas
    const int32_t anchors[] = {100, 230, 394, 500, 800};
    // Small time offsets: 1 block slightly fast
    const int64_t deltas[] = {60, 120, 300, 600};

    for (int32_t anchor : anchors) {
        for (int64_t delta : deltas) {
            // Forward: blocks arrived delta seconds too fast
            int64_t timeDiff_fwd = TESTNET_PARAMS.targetSpacing - delta;
            ASERTResult r1 = CalculateFACTORASERT(TESTNET_PARAMS, anchor,
                                                    timeDiff_fwd, 1);

            // Reverse: use r1.nBits as anchor, apply opposite offset
            int64_t timeDiff_rev = TESTNET_PARAMS.targetSpacing + delta;
            ASERTResult r2 = CalculateFACTORASERT(TESTNET_PARAMS, r1.nBits,
                                                    timeDiff_rev, 1);
            BOOST_CHECK_EQUAL(r2.nBits, anchor);
        }
    }
}

// Sweep all 511 anchor values with many exponents. Verify:
// - output is even
// - output is in [nBitsMin, nBitsMax]
// - output is the closest table entry to the computed target
BOOST_AUTO_TEST_CASE(exhaustive_sweep) {
    // Use compact params to make sweep fast and cover wide exponent range
    const FACTORASERTParams params = {300, 3600, 2, 1022};

    for (int32_t anchor = 2; anchor <= 1022; anchor += 2) {
        const int anchorIdx = anchor / 2 - 1;
        const int64_t anchor_log2 = LOG2_COMPUTE_TABLE[anchorIdx];

        // Sweep exponents from -20 to +20 half-lives in 0.04 half-life steps
        for (int step = -500; step <= 500; ++step) {
            // timeDiff that produces exponent = step * 0.04 half-lives
            // time_error = heightDiff * spacing - timeDiff
            // exponent = time_error / halfLife
            // We want exponent = step * 0.04, so time_error = step * 0.04 * halfLife
            // With heightDiff=1: timeDiff = spacing - time_error
            const int64_t heightDiff = 1;
            double desired_halflives = step * 0.04;
            int64_t time_error = static_cast<int64_t>(desired_halflives * params.halfLife);
            int64_t timeDiff = heightDiff * params.targetSpacing - time_error;

            ASERTResult r = CalculateFACTORASERT(params, anchor, timeDiff, heightDiff);

            // Output must be even and in bounds
            BOOST_CHECK_EQUAL(r.nBits % 2, 0);
            BOOST_CHECK_GE(r.nBits, params.nBitsMin);
            BOOST_CHECK_LE(r.nBits, params.nBitsMax);

            // Verify closest entry (skip clamped boundaries)
            if (r.nBits > params.nBitsMin && r.nBits < params.nBitsMax) {
                const int rIdx = r.nBits / 2 - 1;
                // Recompute exact target for verification
                int64_t te = heightDiff * params.targetSpacing - timeDiff;
                int64_t ip = te / params.halfLife;
                int64_t rem = te % params.halfLife;
                int64_t exp_q32 = (ip << 32) + ((rem << 32) / params.halfLife);
                int64_t target = anchor_log2 + exp_q32;

                int64_t dist_result = std::abs(target - LOG2_COMPUTE_TABLE[rIdx]);
                // Check neighbor below
                if (rIdx > 0) {
                    int64_t dist_below = std::abs(target - LOG2_COMPUTE_TABLE[rIdx - 1]);
                    BOOST_CHECK_LE(dist_result, dist_below);
                }
                // Check neighbor above
                if (rIdx < 510) {
                    int64_t dist_above = std::abs(target - LOG2_COMPUTE_TABLE[rIdx + 1]);
                    BOOST_CHECK_LE(dist_result, dist_above);
                }
            }
        }
    }
}

// Anchor near max, large positive exponent → clamps to nBitsMax, clampedHigh=true.
BOOST_AUTO_TEST_CASE(clamped_high) {
    // Anchor at 1020, blocks absurdly fast → should clamp to 1022
    ASERTResult r = CalculateFACTORASERT(MAINNET_PARAMS, 1020,
                                          1800 - 10 * 172800, 1);
    BOOST_CHECK_EQUAL(r.nBits, 1022);
    BOOST_CHECK_EQUAL(r.clampedHigh, true);

    // Anchor at nBitsMin, extreme fast → should clamp to nBitsMax
    r = CalculateFACTORASERT(MAINNET_PARAMS, 2,
                              1800 - 100 * 172800, 1);
    BOOST_CHECK_EQUAL(r.nBits, 1022);
    BOOST_CHECK_EQUAL(r.clampedHigh, true);
}

// Anchor near min, large negative exponent → clamps to nBitsMin, clampedHigh=false.
BOOST_AUTO_TEST_CASE(clamped_low) {
    // Anchor at 4, blocks absurdly slow → should clamp to 2
    ASERTResult r = CalculateFACTORASERT(MAINNET_PARAMS, 4,
                                          1800 + 10 * 172800, 1);
    BOOST_CHECK_EQUAL(r.nBits, 2);
    BOOST_CHECK_EQUAL(r.clampedHigh, false);

    // Anchor at nBitsMax, extreme slow → should clamp to nBitsMin
    r = CalculateFACTORASERT(MAINNET_PARAMS, 1022,
                              1800 + 100 * 172800, 1);
    BOOST_CHECK_EQUAL(r.nBits, 2);
    BOOST_CHECK_EQUAL(r.clampedHigh, false);
}

// timeDiff=0 (block found instantly) and negative timeDiff (before anchor).
BOOST_AUTO_TEST_CASE(zero_and_negative_timediff) {
    const int32_t anchor = 394;

    // timeDiff=0: time_error = 1*1800 - 0 = 1800 > 0 → harder
    ASERTResult r = CalculateFACTORASERT(MAINNET_PARAMS, anchor, 0, 1);
    BOOST_CHECK_GE(r.nBits, anchor);
    BOOST_CHECK_EQUAL(r.nBits % 2, 0);

    // Negative timeDiff: even more positive time_error → even harder
    ASERTResult r2 = CalculateFACTORASERT(MAINNET_PARAMS, anchor, -1000, 1);
    BOOST_CHECK_GE(r2.nBits, r.nBits);
    BOOST_CHECK_EQUAL(r2.nBits % 2, 0);
}

// Large heightDiff values must not cause intermediate overflow.
BOOST_AUTO_TEST_CASE(overflow_large_height) {
    const int32_t anchor = 394;

    // 2^20 blocks (~1M), steady state
    int64_t h = 1LL << 20;
    ASERTResult r = CalculateFACTORASERT(MAINNET_PARAMS, anchor,
                                          h * 1800, h);
    BOOST_CHECK_EQUAL(r.nBits, anchor);
    BOOST_CHECK_EQUAL(r.clampedHigh, false);

    // 2^25 blocks (~33M), steady state
    h = 1LL << 25;
    r = CalculateFACTORASERT(MAINNET_PARAMS, anchor, h * 1800, h);
    BOOST_CHECK_EQUAL(r.nBits, anchor);
    BOOST_CHECK_EQUAL(r.clampedHigh, false);

    // 2^31-1 blocks, steady state (mainnet: ~68 years of blocks)
    h = (1LL << 31) - 1;
    r = CalculateFACTORASERT(MAINNET_PARAMS, anchor, h * 1800, h);
    BOOST_CHECK_EQUAL(r.nBits, anchor);
    BOOST_CHECK_EQUAL(r.clampedHigh, false);

    // 2^20 blocks, slightly behind schedule
    h = 1LL << 20;
    r = CalculateFACTORASERT(MAINNET_PARAMS, anchor,
                              h * 1800 + 172800, h);
    BOOST_CHECK_LT(r.nBits, anchor);
    BOOST_CHECK_EQUAL(r.nBits % 2, 0);
}

// Verify behavior is consistent across mainnet and testnet parameterizations.
BOOST_AUTO_TEST_CASE(params_variation) {
    const int32_t anchor = 394;

    // Steady state works on both
    ASERTResult rm = CalculateFACTORASERT(MAINNET_PARAMS, anchor,
                                           100LL * 1800, 100);
    ASERTResult rt = CalculateFACTORASERT(TESTNET_PARAMS, anchor,
                                           100LL * 300, 100);
    BOOST_CHECK_EQUAL(rm.nBits, anchor);
    BOOST_CHECK_EQUAL(rt.nBits, anchor);

    // One halfLife behind on each → both get easier (lower nBits)
    rm = CalculateFACTORASERT(MAINNET_PARAMS, anchor,
                               100LL * 1800 + 172800, 100);
    rt = CalculateFACTORASERT(TESTNET_PARAMS, anchor,
                               100LL * 300 + 3600, 100);
    BOOST_CHECK_LT(rm.nBits, anchor);
    BOOST_CHECK_LT(rt.nBits, anchor);
    // Both should produce the same nBits since the exponent is -1.0 for both
    BOOST_CHECK_EQUAL(rm.nBits, rt.nBits);

    // One halfLife ahead on each → both get harder (higher nBits)
    rm = CalculateFACTORASERT(MAINNET_PARAMS, anchor,
                               100LL * 1800 - 172800, 100);
    rt = CalculateFACTORASERT(TESTNET_PARAMS, anchor,
                               100LL * 300 - 3600, 100);
    BOOST_CHECK_GT(rm.nBits, anchor);
    BOOST_CHECK_GT(rt.nBits, anchor);
    BOOST_CHECK_EQUAL(rm.nBits, rt.nBits);
}

BOOST_AUTO_TEST_SUITE_END()
