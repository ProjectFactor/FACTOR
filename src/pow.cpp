// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/params.h>
#include <pow.h>
#include <asert_table.h>
#include <primitives/block.h>
#include <uint256.h>

#include <algorithm> //min and max.
#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <logging.h>

//Blake2b, Scrypt and SHA3-512
#include <cryptopp/blake2.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/files.h>
#include <cryptopp/hex.h>
#include <cryptopp/scrypt.h>
#include <cryptopp/secblock.h>
#include <cryptopp/sha3.h>
#include <cryptopp/whrlpool.h>

//Fancy popcount implementation
#include <libpopcnt.h>

// DeploymentActiveAfter
#include <deploymentstatus.h>

static std::atomic<const CBlockIndex *> cachedAnchor{nullptr};

void ResetASERTAnchorBlockCache() noexcept {
    cachedAnchor = nullptr;
}

bool IsASERTEnabled(const Consensus::Params &params,
                    const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return pindexPrev->GetMedianTimePast() >= params.asertActivationTime;
}

/**
 * Returns a pointer to the anchor block used for ASERT.
 * As anchor we use the first block for which IsASERTEnabled() returns true.
 * This block happens to be the last block which was mined under the old DAA
 * rules.
 *
 * This function is meant to be removed some time after the upgrade, once
 * the anchor block is deeply buried, and behind a hard-coded checkpoint.
 *
 * Preconditions: - pindex must not be nullptr
 *                - pindex must satisfy: IsASERTEnabled(params, pindex) == true
 * Postcondition: Returns a pointer to the first (lowest) block for which
 *                IsASERTEnabled is true, and for which IsASERTEnabled(pprev)
 *                is false (or for which pprev is nullptr). The return value may
 *                be pindex itself.
 */
static const CBlockIndex *GetASERTAnchorBlock(const CBlockIndex *const pindex,
                                              const Consensus::Params &params) {
    assert(pindex);

    // - We check if we have a cached result, and if we do and it is really the
    //   ancestor of pindex, then we return it.
    //
    // - If we do not or if the cached result is not the ancestor of pindex,
    //   then we proceed with the more expensive walk back to find the ASERT
    //   anchor block.
    //
    // CBlockIndex::GetAncestor() is reasonably efficient; it uses CBlockIndex::pskip
    // Note that if pindex == cachedAnchor, GetAncestor() here will return cachedAnchor,
    // which is what we want.
    const CBlockIndex *lastCached = cachedAnchor.load();
    if (lastCached && pindex->GetAncestor(lastCached->nHeight) == lastCached)
        return lastCached;

    // Slow path: walk back until we find the first ancestor for which IsASERTEnabled() == true.
    const CBlockIndex *anchor = pindex;

    while (anchor->pprev) {
        // first, skip backwards testing IsASERTEnabled
        // The below code leverages CBlockIndex::pskip to walk back efficiently.
        if (IsASERTEnabled(params, anchor->pskip)) {
            // skip backward
            anchor = anchor->pskip;
            continue; // continue skipping
        }
        // cannot skip here, walk back by 1
        if (!IsASERTEnabled(params, anchor->pprev)) {
            // found it -- highest block where ASERT is not enabled is anchor->pprev, and
            // anchor points to the first block for which IsASERTEnabled() == true
            break;
        }
        anchor = anchor->pprev;
    }

    // Overwrite the cache with the anchor we found. More likely than not, the next
    // time we are asked to validate a header it will be part of same / similar chain, not
    // some other unrelated chain with a totally different anchor.
    cachedAnchor = anchor;

    return anchor;
}


/**
 * Compute the next required proof of work using an absolutely scheduled
 * exponentially weighted target (ASERT).
 *
 * With ASERT, we define an ideal schedule for block issuance (e.g. 1 block every 600 seconds), and we calculate the
 * difficulty based on how far the most recent block's timestamp is ahead of or behind that schedule.
 * We set our targets (difficulty) exponentially. For every [nHalfLife] seconds ahead of or behind schedule we get, we
 * double or halve the difficulty.
 */
uint32_t GetNextASERTWorkRequired(const CBlockIndex *pindexPrev,
                                  const CBlockHeader *pblock,
                                  const Consensus::Params &params,
                                  const CBlockIndex *pindexAnchorBlock) noexcept {
    // This cannot handle the genesis block and early blocks in general.
    assert(pindexPrev != nullptr);

    // Anchor block is the block on which all ASERT scheduling calculations are based.
    // It too must exist, and it must have a valid parent.
    assert(pindexAnchorBlock != nullptr);

    // We make no further assumptions other than the height of the prev block must be >= that of the anchor block.
    assert(pindexPrev->nHeight >= pindexAnchorBlock->nHeight);

    const arith_uint256 powLimit = arith_uint256(params.powLimit);

    // Special difficulty rule for testnet
    // If the new block's timestamp is more than 2* 10 minutes then allow
    // mining of a min-difficulty block.
    if (params.fPowAllowMinDifficultyBlocks &&
        (pblock->GetBlockTime() >
         pindexPrev->GetBlockTime() + 2 * params.nPowTargetSpacing)) {
        return arith_uint256(params.powLimit).GetCompact();
    }

    // For nTimeDiff calculation, the timestamp of the parent to the anchor block is used,
    // as per the absolute formulation of ASERT.
    // This is somewhat counterintuitive since it is referred to as the anchor timestamp, but
    // as per the formula the timestamp of block M-1 must be used if the anchor is M.
    assert(pindexPrev->pprev != nullptr);
    // Note: time difference is to parent of anchor block (or to anchor block itself iff anchor is genesis).
    //       (according to absolute formulation of ASERT)
    const auto anchorTime = pindexAnchorBlock->pprev
                                    ? pindexAnchorBlock->pprev->GetBlockTime()
                                    : pindexAnchorBlock->GetBlockTime();
    const int64_t nTimeDiff = pindexPrev->GetBlockTime() - anchorTime;
    // Height difference is from current block to anchor block
    const int64_t nHeightDiff = pindexPrev->nHeight - pindexAnchorBlock->nHeight;
    const arith_uint256 refBlockTarget = arith_uint256().SetCompact(pindexAnchorBlock->nBits);
    // Do the actual target adaptation calculation in separate
    // CalculateASERT() function
    arith_uint256 nextTarget = CalculateASERT(refBlockTarget,
                                              params.nPowTargetSpacing,
                                              nTimeDiff,
                                              nHeightDiff,
                                              powLimit,
                                              params.nASERTHalfLife);

    // CalculateASERT() already clamps to powLimit.
    return nextTarget.GetCompact();
}

// ASERT calculation function.
// Clamps to powLimit.
arith_uint256 CalculateASERT(const arith_uint256 &refTarget,
                             const int64_t nPowTargetSpacing,
                             const int64_t nTimeDiff,
                             const int64_t nHeightDiff,
                             const arith_uint256 &powLimit,
                             const int64_t nHalfLife) noexcept {

    // Input target must never be zero nor exceed powLimit.
    assert(refTarget > 0 && refTarget <= powLimit);

    // We need some leading zero bits in powLimit in order to have room to handle
    // overflows easily. 32 leading zero bits is more than enough.
    assert((powLimit >> 224) == 0);

    // Height diff should NOT be negative.
    assert(nHeightDiff >= 0);

    // It will be helpful when reading what follows, to remember that
    // nextTarget is adapted from anchor block target value.

    // Ultimately, we want to approximate the following ASERT formula, using only integer (fixed-point) math:
    //     new_target = old_target * 2^((blocks_time - IDEAL_BLOCK_TIME * (height_diff + 1)) / nHalfLife)

    // First, we'll calculate the exponent:
    assert( llabs(nTimeDiff - nPowTargetSpacing * nHeightDiff) < (1ll << (63 - 16)) );
    const int64_t exponent = ((nTimeDiff - nPowTargetSpacing * (nHeightDiff + 1)) * 65536) / nHalfLife;

    // Next, we use the 2^x = 2 * 2^(x-1) identity to shift our exponent into the [0, 1) interval.
    // The truncated exponent tells us how many shifts we need to do
    // Note1: This needs to be a right shift. Right shift rounds downward (floored division),
    //        whereas integer division in C++ rounds towards zero (truncated division).
    // Note2: This algorithm uses arithmetic shifts of negative numbers. This
    //        is unpecified but very common behavior for C++ compilers before
    //        C++20, and standard with C++20. We must check this behavior e.g.
    //        using static_assert.
    static_assert(int64_t(-1) >> 1 == int64_t(-1),
                  "ASERT algorithm needs arithmetic shift support");

    // Now we compute an approximated target * 2^(exponent/65536.0)

    // First decompose exponent into 'integer' and 'fractional' parts:
    int64_t shifts = exponent >> 16;
    const auto frac = uint16_t(exponent);
    assert(exponent == (shifts * 65536) + frac);

    // multiply target by 65536 * 2^(fractional part)
    // 2^x ~= (1 + 0.695502049*x + 0.2262698*x**2 + 0.0782318*x**3) for 0 <= x < 1
    // Error versus actual 2^x is less than 0.013%.
    const uint32_t factor = 65536 + ((
        + 195766423245049ull * frac
        + 971821376ull * frac * frac
        + 5127ull * frac * frac * frac
        + (1ull << 47)
        ) >> 48);
    // this is always < 2^241 since refTarget < 2^224
    arith_uint256 nextTarget = refTarget * factor;

    // multiply by 2^(integer part) / 65536
    shifts -= 16;
    if (shifts <= 0) {
        nextTarget >>= -shifts;
    } else {
        // Detect overflow that would discard high bits
        const auto nextTargetShifted = nextTarget << shifts;
        if ((nextTargetShifted >> shifts) != nextTarget) {
            // If we had wider integers, the final value of nextTarget would
            // be >= 2^256 so it would have just ended up as powLimit anyway.
            nextTarget = powLimit;
        } else {
            // Shifting produced no overflow, can assign value
            nextTarget = nextTargetShifted;
        }
    }

    if (nextTarget == 0) {
        // 0 is not a valid target, but 1 is.
        nextTarget = arith_uint256(1);
    } else if (nextTarget > powLimit) {
        nextTarget = powLimit;
    }

    // we return from only 1 place for copy elision
    return nextTarget;
}

// ============================================================================
// FACTOR ASERT — operates in log2(compute) space via precomputed table.
//
// Sign convention (OPPOSITE of BCH's target-based formulation):
//   Blocks too FAST → time_error > 0 → positive exponent → nBits UP (harder)
//   Blocks too SLOW → time_error < 0 → negative exponent → nBits DOWN (easier)
// ============================================================================

ASERTResult CalculateFACTORASERT(const FACTORASERTParams& params,
                                 int32_t anchorNBits,
                                 int64_t timeDiff,
                                 int64_t heightDiff) {
    // --- Input validation ---
    assert(anchorNBits % 2 == 0);
    assert(anchorNBits >= params.nBitsMin && anchorNBits <= params.nBitsMax);
    assert(heightDiff >= 0);
    assert(params.halfLife > 0);
    assert(params.targetSpacing > 0);
    assert(params.nBitsMin >= 2 && params.nBitsMin % 2 == 0);
    assert(params.nBitsMax <= 1022 && params.nBitsMax % 2 == 0);
    assert(params.nBitsMin <= params.nBitsMax);

    // --- Step 1: Look up anchor's log2(compute) ---
    const int anchorIdx = anchorNBits / 2 - 1;
    const int64_t anchor_log2_compute = LOG2_COMPUTE_TABLE[anchorIdx];

    // --- Step 2: Compute exponent in Q32.32 ---
    // time_error > 0 when blocks arrive faster than expected (need harder)
    const int64_t expected_elapsed = heightDiff * params.targetSpacing;
    const int64_t time_error = expected_elapsed - timeDiff;

    // Overflow-safe Q32.32 conversion via split division.
    // Direct (time_error << 32) overflows int64_t at |time_error| > ~2.15e9.
    // This variant overflows only at |time_error| > 2^31 * halfLife (~11.7M years
    // on mainnet), effectively eliminating the concern.
    const int64_t integer_part = time_error / params.halfLife;
    const int64_t remainder    = time_error % params.halfLife;
    const int64_t exponent_q32 = (integer_part << 32)
                               + ((remainder << 32) / params.halfLife);

    // --- Step 3: Compute target log2_compute ---
    const int64_t target_log2_compute = anchor_log2_compute + exponent_q32;

    // --- Step 4: Binary search for closest table entry ---
    const int idxMin = params.nBitsMin / 2 - 1;
    const int idxMax = params.nBitsMax / 2 - 1;

    // Check clampedHigh BEFORE clamping the search result.
    // This detects when the ASERT exponent pushes past what nBitsMax represents.
    const bool clampedHigh = (target_log2_compute > LOG2_COMPUTE_TABLE[idxMax]);

    int resultIdx;
    if (target_log2_compute <= LOG2_COMPUTE_TABLE[idxMin]) {
        resultIdx = idxMin;
    } else if (target_log2_compute >= LOG2_COMPUTE_TABLE[idxMax]) {
        resultIdx = idxMax;
    } else {
        // Binary search: find lo such that TABLE[lo] <= target < TABLE[lo+1]
        int lo = idxMin, hi = idxMax;
        while (lo + 1 < hi) {
            int mid = lo + (hi - lo) / 2;
            if (LOG2_COMPUTE_TABLE[mid] <= target_log2_compute) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        // Pick the closer of the two adjacent entries.
        // Tiebreak: lower nBits (lower index = easier difficulty).
        const int64_t dist_lo = target_log2_compute - LOG2_COMPUTE_TABLE[lo];
        const int64_t dist_hi = LOG2_COMPUTE_TABLE[hi] - target_log2_compute;
        resultIdx = (dist_lo <= dist_hi) ? lo : hi;
    }

    const int32_t newNBits = (resultIdx + 1) * 2;
    return ASERTResult{newNBits, clampedHigh};
}

uint16_t GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

    if (IsASERTEnabled(params, pindexLast)) {
        const CBlockIndex *panchorBlock = GetASERTAnchorBlock(pindexLast, params);

        return GetNextASERTWorkRequired(pindexLast, pblock, params, panchorBlock);
    }

    // Check if interim DAA is active
    const bool isInterimDAAActive = TimeLimitedDeploymentActive(pindexLast, params, Consensus::DEPLOYMENT_INTERIM_DAA);

    // Use 42-block interval when interim DAA active, otherwise standard
    const int64_t adjustmentInterval = isInterimDAAActive
        ? Consensus::INTERIM_DAA_PERIOD
        : params.DifficultyAdjustmentInterval();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight + 1) % adjustmentInterval != 0) {
        if (params.fPowAllowMinDifficultyBlocks) {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2) {
                return params.powLimit;
            } else {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % adjustmentInterval != 0 &&
                       pindex->nBits == params.powLimit) {
                    pindex = pindex->pprev;
                }
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Get first block of period
    int nHeightFirst = pindexLast->nHeight - (adjustmentInterval - 1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    // Calculate target timespan based on active DAA
    // Use (INTERIM_DAA_PERIOD - 1) because right now we don't know the time it took to mine the last block.
    const int64_t nTargetTimespan = isInterimDAAActive
        ? ((Consensus::INTERIM_DAA_PERIOD - 1) * params.nPowTargetSpacing)
        : params.nPowTargetTimespan;

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params, nTargetTimespan, isInterimDAAActive);
}

int32_t CalculateDifficultyDelta(const int32_t nBits, const double nPeriodTimeProportionConsumed, const bool isHardDiffRemoved) {
    if (!isHardDiffRemoved) {
        // Original difficulty adjustment algorithm

        //Note for mainnet:
        //If it takes more than 1 minute over the target blocktime, reduce difficulty.
        if (nPeriodTimeProportionConsumed > 1.0333f)
            return -1;

        //Note for mainnet:
        //To increase difficulty the network must be able to move the blocktime
        //3 minutes under target blocktime. This is to avoid the difficulty becoming
        //too much work for the network to handle. Based on heuristics.
        if (nPeriodTimeProportionConsumed < 0.90f)
            return 1;

        return 0;
    } else {
        // Difficulty adjustment algorithm that skips over odd aka. hard diffs (2025)

        // If block time is too long, decrease to the previous even diff
        if (nPeriodTimeProportionConsumed > 1.0333f) {
            int32_t nRetarget = 0;
            if (nBits % 2 == 0) {
                // Even diff to even diff
                nRetarget = -2;
            } else {
                // Odd diff to even diff
                nRetarget = -1;
            }

            // If block time is way too long (>60 min on mainnet), decrease by 4 or 3 instead of by 2 or 1
            if (nPeriodTimeProportionConsumed > 2.0f) {
                nRetarget -= 2;
            }

            return nRetarget;
        }

        // If block time is too short, increase to the next even diff
        if (nPeriodTimeProportionConsumed < 0.90f) {
            int32_t nRetarget = 0;
            if (nBits % 2 == 0) {
                // Even diff to even diff
                nRetarget = 2;
            } else {
                // Odd diff to even diff
                nRetarget = 1;
            }

            // If block time is way too short (<15 min on mainnet), increase by 4 or 3 instead of by 2 or 1
            if (nPeriodTimeProportionConsumed < 0.5f) {
                nRetarget += 2;
            }

            return nRetarget;
        }

        // The block time is just right. See if we should move away from an odd diff
        if (nBits % 2 == 0) {
            // Already even diff. Don't change difficulty
            return 0;
        } else {
            // Currently odd diff. Decrease diff by 1 to reach an even difficulty
            return -1;
        }
    }
}

/**
 * Interim DAA: proportion-based difficulty adjustment with aggressive thresholds.
 * Uses nPeriodTimeProportionConsumed like existing DAA but with different thresholds.
 * Also ensures result skips odd difficulties (like HARD_DIFF_REMOVAL mode).
 *
 * @param nBits Current difficulty bits
 * @param nPeriodTimeProportionConsumed Ratio of actual time to target time
 */
int32_t CalculateInterimDifficultyDelta(const int32_t nBits, const double nPeriodTimeProportionConsumed)
{
    int32_t nRetarget = 0;

    // Make sure we're going to an even aka. easy difficulty
    if (nBits % 2 != 0) {
        nRetarget -= 1;
    }

    // Too fast, increase difficulty
    // 0.5 = 15min avg, 0.6667 ~= 20min avg, 0.9 = 27min avg for 30min target
    if (nPeriodTimeProportionConsumed < 0.5f) {
        nRetarget += 6;
    } else if (nPeriodTimeProportionConsumed < 0.6667f) {
        nRetarget += 4;
    } else if (nPeriodTimeProportionConsumed < 0.9f) {
        nRetarget += 2;
    }

    // Too slow, decrease difficulty
    // 2.0 = 60min avg, 1.5 = 45min avg, 1.0333 ~= 31min avg for 30min target
    else if (nPeriodTimeProportionConsumed > 2.0f) {
        nRetarget -= 6;
    } else if (nPeriodTimeProportionConsumed > 1.5f) {
        nRetarget -= 4;
    } else if (nPeriodTimeProportionConsumed > 1.0333f) {
        nRetarget -= 2;
    }

    return nRetarget;
}

uint16_t CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params,
                                   int64_t nTargetTimespan, bool isInterimDAA)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Use provided target timespan or default
    if (nTargetTimespan == 0) {
        nTargetTimespan = params.nPowTargetTimespan;
    }

    // Compute constants
    const int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    const double nPeriodTimeProportionConsumed = (double)nActualTimespan / (double)nTargetTimespan;

    int32_t nRetarget;
    if (isInterimDAA) {
        // Use interim DAA algorithm
        nRetarget = CalculateInterimDifficultyDelta(pindexLast->nBits, nPeriodTimeProportionConsumed);
    } else {
        // Use standard algorithm (with HARD_DIFF_REMOVAL check)
        const bool isHardDiffRemoved = DeploymentActiveAfter(pindexLast, params, Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL);
        nRetarget = CalculateDifficultyDelta(pindexLast->nBits, nPeriodTimeProportionConsumed, isHardDiffRemoved);
    }

    return (int32_t)pindexLast->nBits + nRetarget;
}

bool CheckProofOfWork(const CBlockHeader& block, const Consensus::Params& params)
{
    if (block.nBits == 0 || block.nBits > 1023) {
        LogPrintf("PoW error: nBits %d out of valid range [1, 1023]\n", block.nBits);
        return false;
    }

    //First, generate the random seed submited for this block
    uint1024 w = gHash(block, params);

    //Check that |block->offset| <= \tilde{n} = 16 * |n|_2.
    uint64_t abs_offset = (block.wOffset > 0) ? block.wOffset : -block.wOffset;

    if (abs_offset > 16 * block.nBits) {
        LogPrintf("PoW error: invalid wOffset\n");
        return false;
    }

    //Get the semiprime n
    mpz_t n, W;
    mpz_init(n);
    mpz_init(W);
    mpz_import(W, 16, -1, 8, 0, 0, w.u64_begin());

    //Add the offset to w to find the semiprime submitted: n = w + offset
    if (block.wOffset >= 0) {
        mpz_add_ui(n, W, abs_offset);
    } else {
        mpz_sub_ui(n, W, abs_offset);
    }

    {
        char *w_str = mpz_get_str(NULL, 10, W);
        char *n_str = mpz_get_str(NULL, 10, n);
        LogPrint(BCLog::POW, "  W: %s\n", w_str);
        LogPrint(BCLog::POW, "  N: %s\n", n_str);
        free(w_str);
        free(n_str);
    }

    //Clear memory for W.
    mpz_clear(W);

    //Check the number n has nBits
    if (mpz_sizeinbase(n, 2) != block.nBits) {
        LogPrintf("pow error: invalid nBits: expected %d, actual %d\n", block.nBits, mpz_sizeinbase(n, 2));
        mpz_clear(n);
        return false;
    }

    //Divide the factor submitted for this block by N
    mpz_t nP1, nP2;
    mpz_init(nP1);
    mpz_init(nP2);
    mpz_import(nP1, 16, -1, 8, 0, 0, block.nP1.u64_begin());

    if (mpz_sgn(nP1) == 0) {
        LogPrintf("PoW error: nP1 is zero\n");
        mpz_clear(n);
        mpz_clear(nP1);
        mpz_clear(nP2);
        return false;
    }

    mpz_tdiv_q(nP2, n, nP1);

    {
        char *p1_str = mpz_get_str(NULL, 10, nP1);
        char *p2_str = mpz_get_str(NULL, 10, nP2);
        LogPrint(BCLog::POW, "nP1: %s\n", p1_str);
        LogPrint(BCLog::POW, "nP2: %s\n", p2_str);
        free(p1_str);
        free(p2_str);
    }

    //Check the bitsizes are as expected
    const uint16_t nP1_bitsize = mpz_sizeinbase(nP1, 2);
    const uint16_t expected_bitsize = (block.nBits >> 1) + (block.nBits & 1);

    if (nP1_bitsize != expected_bitsize) {
        LogPrintf("pow error: nP1 expected bitsize=%s, actual size=%s\n", expected_bitsize, nP1_bitsize);
        mpz_clear(n);
        mpz_clear(nP1);
        mpz_clear(nP2);
        return false;
    }

    //Check nP1 is a factor
    mpz_t n_check;
    mpz_init(n_check);
    mpz_mul(n_check, nP1, nP2);

    //Check that nP1*nP2 == n.
    if (mpz_cmp(n_check, n) != 0) {
        {
            char *n_str = mpz_get_str(NULL, 10, n);
            char *p1_str = mpz_get_str(NULL, 10, nP1);
            LogPrintf("pow error: nP1 does not divide N.  N=%s nP1=%s\n", n_str, p1_str);
            free(n_str);
            free(p1_str);
        }
        mpz_clear(n);
        mpz_clear(nP1);
        mpz_clear(nP2);
        mpz_clear(n_check);
        return false;
    }

    //Check that nP1 <= nP2.
    if (mpz_cmp(nP1, nP2) > 0) {
        {
            char *n_str = mpz_get_str(NULL, 10, n);
            char *p1_str = mpz_get_str(NULL, 10, nP1);
            LogPrintf("pow error: nP1 must be the smallest factor. N=%s nP1=%s\n", n_str, p1_str);
            free(n_str);
            free(p1_str);
        }
        mpz_clear(n);
        mpz_clear(nP1);
        mpz_clear(nP2);
        mpz_clear(n_check);
        return false;
    }


    //Clear memory
    mpz_clear(n);
    mpz_clear(n_check);

    //Test nP1 and nP2 for primality.
    int is_nP1_prime = mpz_probab_prime_p(nP1, params.MillerRabinRounds);
    int is_nP2_prime = mpz_probab_prime_p(nP2, params.MillerRabinRounds);

    //Clear memory
    mpz_clear(nP1);
    mpz_clear(nP2);

    //Check they are both prime
    if (is_nP1_prime == 0 || is_nP2_prime == 0) {
        LogPrintf("pow error: At least 1 composite factor found, rejected.\n");
        return false;
    }

    return true;
}

uint1024 gHash(const CBlockHeader& block, const Consensus::Params& params)
{
    //Get the required data for this block
    uint256 hashPrevBlock = block.hashPrevBlock;
    uint256 hashMerkleRoot = block.hashMerkleRoot;
    uint64_t nNonce = block.nNonce;
    uint32_t nTime = block.nTime;
    int32_t nVersion = block.nVersion;
    uint16_t nBits = block.nBits;

    using namespace CryptoPP;

    //Place data as raw bytes into the password and salt for Scrypt:
    /////////////////////////////////////////////////
    // pass = hashPrevBlock + hashMerkle + nNonce  //
    // salt = version       + nBits      + nTime   //
    /////////////////////////////////////////////////
    byte pass[256 / 8 + 256 / 8 + 64 / 8] = {(byte)0};
    byte salt[32 / 8 + 16 / 8 + 32 / 8] = {(byte)0};

    //SALT: Copy version into the first 4 bytes of the salt.
    memcpy(salt, &nVersion, sizeof(nVersion));

    //SALT: Copy nBits into the next 2 bytes
    int runningLen = sizeof(nVersion);
    memcpy(&salt[runningLen], &nBits, sizeof(nBits));

    //SALT: Copy nTime into the next 4 bytes
    runningLen += sizeof(nBits);
    memcpy(&salt[runningLen], &nTime, sizeof(nTime));

    //PASS: Copy Previous Block Hash into the first 32 bytes
    memcpy(pass, hashPrevBlock.begin(), hashPrevBlock.size());

    //PASS: Copy Merkle Root hash into next 32 bytes
    runningLen = hashPrevBlock.size();
    memcpy(&pass[runningLen], hashMerkleRoot.begin(), hashMerkleRoot.size());

    //PASS: Copy nNonce
    runningLen += hashMerkleRoot.size();
    memcpy(&pass[runningLen], &nNonce, sizeof(nNonce));

    ////////////////////////////////////////////////////////////////////////////////
    //                                Scrypt parameters                           //
    ////////////////////////////////////////////////////////////////////////////////
    //                                                                            //
    //  N                  = Iterations count (Affects memory and CPU Usage).     //
    //  r                  = block size ( affects memory and CPU usage).          //
    //  p                  = Parallelism factor. (Number of threads).             //
    //  pass               = Input password.                                      //
    //  salt               = securely-generated random bytes.                     //
    //  derived-key-length = how many bytes to generate as output. Defaults to 32.//
    //                                                                            //
    // For reference, Litecoin has N=1024, r=1, p=1.                              //
    ////////////////////////////////////////////////////////////////////////////////
    Scrypt scrypt;
    word64 N = 1ULL << 12;
    word64 r = 1ULL << 1;
    word64 p = 1ULL;
    SecByteBlock derived(256);

    //Scrypt Hash to 2048-bits hash.
    scrypt.DeriveKey(derived, derived.size(), pass, sizeof(pass), salt, sizeof(salt), N, r, p);

    //Consensus parameters
    int roundsTotal = params.hashRounds;

    //Prepare GMP objects
    mpz_t prime_mpz, starting_number_mpz, a_mpz, a_inverse_mpz;
    mpz_init(prime_mpz);
    mpz_init(starting_number_mpz);
    mpz_init(a_mpz);
    mpz_init(a_inverse_mpz);

    for (int round = 0; round < roundsTotal; round++) {
        ///////////////////////////////////////////////////////////////
        //      Memory Expensive Scrypt: 1MB required.              //
        ///////////////////////////////////////////////////////////////
        scrypt.DeriveKey(derived,                     //Final hash
                         derived.size(),              //Final hash number of bytes
                         (const byte*)derived.data(), //Input hash
                         derived.size(),              //Input hash number of bytes
                         salt,                        //Salt
                         sizeof(salt),                //Salt bytes
                         N,                           //Number of rounds
                         r,                           //Sequential Read Sisze
                         p                            //Parallelizable iterations
        );

        ///////////////////////////////////////////////////////////////
        //   Add different types of hashes to the core.              //
        ///////////////////////////////////////////////////////////////
        //Count the bits in previous hash.
        uint64_t pcnt_half1 = popcnt(derived.data(), 128);
        uint64_t pcnt_half2 = popcnt(&derived.data()[128], 128);

        //Hash the first 1024-bits of the 2048-bits hash.
        if (pcnt_half1 % 2 == 0) {
            BLAKE2b bHash;
            bHash.Update((const byte*)derived.data(), 128);
            bHash.Final((byte*)derived.data());
        } else {
            SHA3_512 bHash;
            bHash.Update((const byte*)derived.data(), 128);
            bHash.Final((byte*)derived.data());
        }

        //Hash the second 1024-bits of the 2048-bits hash.
        if (pcnt_half2 % 2 == 0) {
            BLAKE2b bHash;
            bHash.Update((const byte*)(&derived.data()[128]), 128);
            bHash.Final((byte*)(&derived.data()[128]));
        } else {
            SHA3_512 bHash;
            bHash.Update((const byte*)(&derived.data()[128]), 128);
            bHash.Final((byte*)(&derived.data()[128]));
        }

        //////////////////////////////////////////////////////////////
        // Perform expensive math opertions plus simple hashing     //
        //////////////////////////////////////////////////////////////
        //Use the current hash to compute grunt work.
        mpz_import(starting_number_mpz, 32, -1, 8, 0, 0, derived.data()); // -> M = 2048-hash
        mpz_sqrt(starting_number_mpz, starting_number_mpz);               // - \ a = floor( M^(1/2) )
        mpz_set(a_mpz, starting_number_mpz);                              // - /
        mpz_sqrt(starting_number_mpz, starting_number_mpz);               // - \ p = floor( a^(1/2) )
        mpz_nextprime(prime_mpz, starting_number_mpz);                    // - /

        //Compute a^(-1) Mod p
        mpz_invert(a_inverse_mpz, a_mpz, prime_mpz);

        //Xor into current hash digest.
        size_t words = 0;
        uint64_t data[32] = {0};
        uint64_t* hDigest = (uint64_t*)derived.data();
        mpz_export(data, &words, -1, 8, 0, 0, a_inverse_mpz);
        for (int jj = 0; jj < 32; jj++)
            hDigest[jj] ^= data[jj];

        //Check that at most 2048-bits were written
        //Assume 64-bit limbs.
        assert(words <= 32);

        //Compute the population count of a_inverse
        const int32_t irounds = popcnt(data, sizeof(data)) & 0x7f;

        //Branch away
        for (int jj = 0; jj < irounds; jj++) {
            // sizeof(derived.data()) was sizeof(pointer), not the buffer size.
            // On 64-bit this evaluated to 8, which is now the consensus value.
            // Hardcode 8 so a 32-bit build doesn't fork from the network.
            const int32_t br = popcnt(derived.data(), 8);

            //Power mod
            mpz_powm_ui(a_inverse_mpz, a_inverse_mpz, irounds, prime_mpz);

            //Get the data out of gmp
            mpz_export(data, &words, -1, 8, 0, 0, a_inverse_mpz);
            assert(words <= 32);

            for (int jj = 0; jj < 32; jj++)
                hDigest[jj] ^= data[jj];

            if (br % 3 == 0) {
                SHA3_512 bHash;
                bHash.Update((const byte*)derived.data(), 128);
                bHash.Final((byte*)derived.data());
            } else if (br % 3 == 2) {
                BLAKE2b sHash;
                sHash.Update((const byte*)(&derived.data()[128]), 128);
                sHash.Final((byte*)(&derived.data()[192]));
            } else {
                Whirlpool wHash;
                wHash.Update((const byte*)(derived.data()), 256);
                wHash.Final((byte*)(&derived.data()[112]));
            }
        }
    }

    //Compute how many bytes to copy
    int32_t allBytes = nBits / 8;
    int32_t remBytes = nBits % 8;

    //Make sure to stay within 2048-bits.
    // NOTE: In the distant future this will have to be updated
    //       when nBITS starts to get close to 1024-bits, the limit of type uint1024.
    assert(allBytes + 1 <= 128);

    //TODO: Note that we use here a type to that holds 1024-bits instead of the
    //      2048 bits of the hash above. For now this is fine, eventually, we will
    //      need to implement a 2048-bit when the system is factoring 900+ digit numbers.
    //      As this is unlikely to happen any time soon I think we are fine.
    //Copy exactly the number of bytes that contains exactly the low nBits bits.
    uint1024 w;

    //Make sure the values in w are set to 0.
    memset(w.u8_begin_write(), 0, 128);

    memcpy(w.u8_begin_write(), derived.begin(), std::min(128, allBytes + 1));

    //Trim off any bits from the Most Significant byte.
    w.u8_begin_write()[allBytes] = w.u8_begin()[allBytes] & ((1 << remBytes) - 1);

    //Set the nBits-bit to one.
    if (remBytes == 0) {
        w.u8_begin_write()[allBytes - 1] = w.u8_begin()[allBytes - 1] | 128;
    } else {
        w.u8_begin_write()[allBytes] = w.u8_begin()[allBytes] | (1 << (remBytes - 1));
    }

    mpz_clear(prime_mpz);
    mpz_clear(starting_number_mpz);
    mpz_clear(a_mpz);
    mpz_clear(a_inverse_mpz);

    return w;
}

// f(z) = z^2 + 1 Mod n
void f(mpz_t z, mpz_t n, mpz_t two)
{
    mpz_powm(z, z, two, n);
    mpz_add_ui(z, z, 1ULL);
    mpz_mod(z, z, n);
}

//  Pollard rho factoring algorithm
//  Input: g First found factor goes here
//         n Number to be factored.
// Output: Return  0 if n is prime.
//         Returns 1 if both factors g and n/g terminate being prime.
//         Return  0 otherwise.
int rho(mpz_t g, mpz_t n)
{
    //Parameter of 25 gives 1 in 2^100 chance of false positive. That's good enough.
    if (mpz_probab_prime_p(n, 25) != 0)
        return 0; //test if n is prime with miller rabin test

    mpz_t x;
    mpz_t y;
    mpz_t two;
    mpz_t temp;
    mpz_init_set_ui(x, 2);
    mpz_init_set_ui(y, 2); //initialize x and y as 2
    mpz_init_set_ui(two, 2);
    mpz_set_ui(g, 1);
    mpz_init(temp);

    while (mpz_cmp_ui(g, 1) == 0) {
        f(x, n, two); //x is changed
        f(y, n, two);
        f(y, n, two); //y is going through the sequence twice as fast

        mpz_sub(temp, x, y);
        mpz_gcd(g, temp, n);
    }

    //Set temp = n/g
    mpz_divexact(temp, n, g);

    //Test if g, temp are prime.
    int u_p = mpz_probab_prime_p(temp, 30);
    int g_p = mpz_probab_prime_p(g, 30);

    //Clear gmp memory allocated inside this function
    mpz_clear(x);
    mpz_clear(y);
    mpz_clear(temp);
    mpz_clear(two);

    //Enforce primality checks
    //Check g is a proper factor. Pollard Rho may have missed a factorization.
    if ((u_p != 0) && (g_p != 0) && (mpz_cmp(g, n) != 0))
        return 1;

    return 0;
}

int rho(uint64_t& g, uint64_t n)
{
    //Declare needed types
    mpz_t g1;
    mpz_t n1;

    //Initialize gmp types
    mpz_init(g1);
    mpz_init(n1);

    //Set gmp types
    mpz_set_ui(g1, g);
    mpz_set_ui(n1, n);

    //Factor and capture return value
    int result = rho(g1, n1);
    g = mpz_get_ui(g1);

    //Clear memory allocated inside this function
    mpz_clear(g1);
    mpz_clear(n1);

    //return
    return result;
}
