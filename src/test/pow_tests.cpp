// Copyright (c) 2015-2020 The Bitcoin Core developers
// Copyright (c) 2026 The FACTOR developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>


// Build a mock chain of CBlockIndex objects for testing GetNextWorkRequired.
// genesis_time: timestamp of the genesis block (height 0)
// genesis_nBits: nBits of the genesis block
// count: total number of blocks (including genesis)
// spacing: time between consecutive blocks
static std::vector<CBlockIndex> BuildMockChain(int64_t genesis_time,
                                               uint16_t genesis_nBits,
                                               int count,
                                               int64_t spacing) {
    std::vector<CBlockIndex> chain(count);
    for (int i = 0; i < count; i++) {
        chain[i].nHeight = i;
        chain[i].nTime = genesis_time + i * spacing;
        chain[i].nBits = genesis_nBits;
        chain[i].pprev = (i > 0) ? &chain[i - 1] : nullptr;
        chain[i].pskip = nullptr;
    }
    return chain;
}

struct TestnetTestingSetup : public BasicTestingSetup {
    TestnetTestingSetup() : BasicTestingSetup(CBaseChainParams::TESTNET) {}
};

BOOST_FIXTURE_TEST_SUITE(pow_tests, TestnetTestingSetup)

// Blocks at height <= 1 return genesis nBits directly (height guard).
// Block at height 2 is the first ASERT-computed block.
BOOST_AUTO_TEST_CASE(height_guard) {
    const Consensus::Params& params = Params().GetConsensus();
    CBlockHeader header;

    // Build chain: genesis (height 0) + block 1 (anchor)
    auto chain = BuildMockChain(1650443545, 32, 2, params.nPowTargetSpacing);

    // Computing difficulty for block 1 (pindexLast = genesis at height 0):
    // height guard returns genesis nBits
    uint16_t nBits = GetNextWorkRequired(&chain[0], &header, params);
    BOOST_CHECK_EQUAL(nBits, 32);

    // Computing difficulty for block 2 (pindexLast = block 1 at height 1):
    // ASERT kicks in. Block 1 is anchor; heightDiff=0, timeDiff=0 → steady state
    nBits = GetNextWorkRequired(&chain[1], &header, params);
    BOOST_CHECK_EQUAL(nBits, 32);
}

// 10 blocks at exactly target spacing → difficulty stays at genesis nBits.
BOOST_AUTO_TEST_CASE(steady_state_chain) {
    const Consensus::Params& params = Params().GetConsensus();
    CBlockHeader header;

    auto chain = BuildMockChain(1650443545, 32, 15, params.nPowTargetSpacing);

    // Every block from height 2 onward should get nBits=32 (steady state)
    for (int i = 1; i < 14; i++) {
        uint16_t nBits = GetNextWorkRequired(&chain[i], &header, params);
        BOOST_CHECK_EQUAL(nBits, 32);
    }
}

// Blocks arriving faster than target spacing → nBits should increase (harder).
BOOST_AUTO_TEST_CASE(fast_blocks_increase_difficulty) {
    const Consensus::Params& params = Params().GetConsensus();
    CBlockHeader header;

    // Build 15 blocks at half the target spacing (150s instead of 300s)
    auto chain = BuildMockChain(1650443545, 32, 15, params.nPowTargetSpacing / 2);

    // After enough fast blocks, difficulty should increase above genesis
    uint16_t nBits = GetNextWorkRequired(&chain[14], &header, params);
    BOOST_CHECK_GT(nBits, 32);
}

// Blocks arriving slower than target spacing → nBits should decrease (easier).
// Since genesis is already at nBitsMin=32, ASERT clamps to 32.
// Use a higher anchor to verify the decrease.
BOOST_AUTO_TEST_CASE(slow_blocks_decrease_difficulty) {
    const Consensus::Params& params = Params().GetConsensus();
    CBlockHeader header;

    // Build 15 blocks at double target spacing (600s instead of 300s)
    // Start with higher nBits on the anchor (block 1) to have room to decrease
    auto chain = BuildMockChain(1650443545, 32, 15, params.nPowTargetSpacing * 2);

    // Manually set block 1's nBits higher so ASERT has room to decrease
    chain[1].nBits = 100;

    // After slow blocks from an nBits=100 anchor, difficulty should decrease
    uint16_t nBits = GetNextWorkRequired(&chain[14], &header, params);
    BOOST_CHECK_LT(nBits, 100);
}

// Circuit breaker: when ASERT pushes past nBitsMax, return 1024.
BOOST_AUTO_TEST_CASE(circuit_breaker) {
    const Consensus::Params& params = Params().GetConsensus();
    CBlockHeader header;

    // Build a chain where blocks arrive absurdly fast — many blocks in
    // almost no time, pushing the exponent so high that it clamps past 1022.
    // Anchor at nBits=1000 (near max), then 200 blocks in 1 second each.
    auto chain = BuildMockChain(1650443545, 32, 202, 1);
    chain[1].nBits = 1000;

    uint16_t nBits = GetNextWorkRequired(&chain[201], &header, params);
    BOOST_CHECK_EQUAL(nBits, 1024);
}

// fPowAllowMinDifficultyBlocks escape hatch must NOT activate under ASERT.
// Verify with difficulty above nBitsMin so the hatch would be observable.
BOOST_AUTO_TEST_CASE(escape_hatch_disabled_under_asert) {
    const Consensus::Params& params = Params().GetConsensus();
    BOOST_REQUIRE(params.fPowAllowMinDifficultyBlocks);

    // Build a chain where blocks arrive fast, pushing difficulty up.
    // Then the last block has a timestamp far in the future (>2x spacing).
    auto chain = BuildMockChain(1650443545, 32, 15, params.nPowTargetSpacing / 2);

    // Get the ASERT difficulty at block 14 to confirm it's above nBitsMin
    CBlockHeader header;
    uint16_t normalBits = GetNextWorkRequired(&chain[13], &header, params);
    BOOST_REQUIRE_GT(normalBits, params.nBitsMin);

    // Set block 14's nBits to the ASERT-computed value (as if it were mined)
    chain[14].nBits = normalBits;

    // Now create a block whose timestamp is far in the future (>2x spacing)
    CBlockHeader futureHeader;
    futureHeader.nTime = chain[14].nTime + params.nPowTargetSpacing * 10;

    uint16_t nBits = GetNextWorkRequired(&chain[14], &futureHeader, params);

    // Under the old DAA, this would have returned powLimit (32).
    // Under ASERT, it must NOT return powLimit — difficulty stays ASERT-computed.
    BOOST_CHECK_GT(nBits, params.nBitsMin);
}

// All nBits outputs should be even.
BOOST_AUTO_TEST_CASE(output_always_even) {
    const Consensus::Params& params = Params().GetConsensus();
    CBlockHeader header;

    // Test a range of spacings to produce varied nBits values
    int64_t spacings[] = {1, 60, 150, 300, 600, 1200};
    for (int64_t sp : spacings) {
        auto chain = BuildMockChain(1650443545, 32, 15, sp);
        for (int i = 1; i < 14; i++) {
            uint16_t nBits = GetNextWorkRequired(&chain[i], &header, params);
            BOOST_CHECK_EQUAL(nBits % 2, 0);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()


/*
// ============================================================================
// Anchor discovery tests — exercises GetASERTAnchorBlock via mainnet params.
// ============================================================================

// Like BuildMockChain but populates pskip (required by GetASERTAnchorBlock).
static std::vector<CBlockIndex> BuildMockChainWithSkip(int64_t genesis_time,
                                                       uint16_t genesis_nBits,
                                                       int count,
                                                       int64_t spacing) {
    std::vector<CBlockIndex> chain(count);
    for (int i = 0; i < count; i++) {
        chain[i].nHeight = i;
        chain[i].nTime = genesis_time + i * spacing;
        chain[i].nBits = genesis_nBits;
        chain[i].pprev = (i > 0) ? &chain[i - 1] : nullptr;
        chain[i].BuildSkip();
    }
    return chain;
}

struct MainnetTestingSetup : public BasicTestingSetup {
    MainnetTestingSetup() : BasicTestingSetup(CBaseChainParams::MAIN) {}
};

BOOST_FIXTURE_TEST_SUITE(pow_anchor_tests, MainnetTestingSetup)

// Chain crosses activation boundary at block 5. Each block gets a unique
// nBits so we can identify exactly which block was chosen as anchor.
BOOST_AUTO_TEST_CASE(anchor_discovery_at_boundary) {
    const Consensus::Params& params = Params().GetConsensus();
    CBlockHeader header;
    ResetASERTAnchorBlockCache();

    // Place activation boundary at block 5
    const int64_t genesis_time = params.asertActivationTime - 5 * params.nPowTargetSpacing;
    auto chain = BuildMockChainWithSkip(genesis_time, 32, 20, params.nPowTargetSpacing);

    // Give every block a unique even nBits in [32, 70]
    for (int i = 0; i < 20; i++) {
        chain[i].nBits = static_cast<uint16_t>(32 + 2 * i);
    }
    // Block 5 (the anchor) has nBits = 32 + 10 = 42

    // Query from block 6 (first block after anchor): anchor is block 5,
    // heightDiff=1, timeDiff=1*spacing → steady state → returns anchor nBits
    uint16_t nBits = GetNextWorkRequired(&chain[6], &header, params);
    BOOST_CHECK_EQUAL(nBits, 42);

    // Query from block 10: same anchor
    nBits = GetNextWorkRequired(&chain[10], &header, params);
    BOOST_CHECK_EQUAL(nBits, 42);

    // Query from block 15: anchor must still be block 5
    nBits = GetNextWorkRequired(&chain[15], &header, params);
    BOOST_CHECK_EQUAL(nBits, 42);
}

// Two independent chains with different anchor nBits. The atomic cache
// must be invalidated when switching chains (different pointer identity).
BOOST_AUTO_TEST_CASE(anchor_cache_reorg) {
    const Consensus::Params& params = Params().GetConsensus();
    CBlockHeader header;
    ResetASERTAnchorBlockCache();

    const int64_t genesis_time = params.asertActivationTime - 5 * params.nPowTargetSpacing;

    // Chain A: anchor (block 5) has nBits=100
    auto chainA = BuildMockChainWithSkip(genesis_time, 32, 20, params.nPowTargetSpacing);
    chainA[5].nBits = 100;

    // Chain B: anchor (block 5) has nBits=200
    auto chainB = BuildMockChainWithSkip(genesis_time, 32, 20, params.nPowTargetSpacing);
    chainB[5].nBits = 200;

    // Query chain A — populates cache with &chainA[5]
    uint16_t nBitsA = GetNextWorkRequired(&chainA[10], &header, params);
    BOOST_CHECK_EQUAL(nBitsA, 100);

    // Query chain B — cache must invalidate (different pointer)
    uint16_t nBitsB = GetNextWorkRequired(&chainB[10], &header, params);
    BOOST_CHECK_EQUAL(nBitsB, 200);

    // Query chain A again — cache was overwritten by chain B's anchor,
    // must invalidate again and re-find chain A's anchor
    uint16_t nBitsA2 = GetNextWorkRequired(&chainA[15], &header, params);
    BOOST_CHECK_EQUAL(nBitsA2, 100);
}

// Post-activation blocks arriving near-instantly → difficulty increases.
BOOST_AUTO_TEST_CASE(anchor_fast_blocks) {
    const Consensus::Params& params = Params().GetConsensus();
    CBlockHeader header;
    ResetASERTAnchorBlockCache();

    const int64_t genesis_time = params.asertActivationTime - 5 * params.nPowTargetSpacing;
    auto chain = BuildMockChainWithSkip(genesis_time, 32, 200, params.nPowTargetSpacing);

    // 1-second spacing post-activation to generate a large positive exponent
    for (int i = 6; i < 200; i++) {
        chain[i].nTime = chain[5].nTime + (i - 5);
    }

    uint16_t nBits = GetNextWorkRequired(&chain[199], &header, params);
    BOOST_CHECK_GT(nBits, 32);
}

// Post-activation blocks at double spacing → difficulty decreases.
BOOST_AUTO_TEST_CASE(anchor_slow_blocks) {
    const Consensus::Params& params = Params().GetConsensus();
    CBlockHeader header;
    ResetASERTAnchorBlockCache();

    const int64_t genesis_time = params.asertActivationTime - 5 * params.nPowTargetSpacing;
    auto chain = BuildMockChainWithSkip(genesis_time, 32, 20, params.nPowTargetSpacing);

    // Anchor at nBits=100 so there's room to decrease
    chain[5].nBits = 100;

    // Double the spacing for post-activation blocks
    for (int i = 6; i < 20; i++) {
        chain[i].nTime = chain[5].nTime + (i - 5) * (params.nPowTargetSpacing * 2);
    }

    uint16_t nBits = GetNextWorkRequired(&chain[15], &header, params);
    BOOST_CHECK_LT(nBits, 100);
}

BOOST_AUTO_TEST_SUITE_END()
*/
