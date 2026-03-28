// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <arith_uint256.h>
#include <consensus/params.h>
#include <stdint.h>
#include <gmp.h>
#include <gmpxx.h>

class CBlockHeader;
class CBlockIndex;
class uint256;

/** Result of the FACTOR ASERT difficulty calculation. */
struct ASERTResult {
    int32_t nBits;     ///< New difficulty (always even, in [nBitsMin, nBitsMax])
    bool clampedHigh;  ///< True if unclamped result would have exceeded nBitsMax
};

/** Parameters for the FACTOR ASERT DAA. */
struct FACTORASERTParams {
    int64_t targetSpacing;  ///< Target time between blocks, in seconds
    int64_t halfLife;       ///< ASERT half-life, in seconds
    int32_t nBitsMin;       ///< Minimum allowed nBits (even, >= 2)
    int32_t nBitsMax;       ///< Maximum allowed nBits (even, <= 1022)
};

/**
 * Compute the next FACTOR nBits using the ASERT algorithm.
 *
 * Pure function — no chain-state dependencies. Works in log2(compute(nBits))
 * space via a precomputed lookup table with Q32.32 fixed-point arithmetic.
 *
 * Sign convention: a POSITIVE exponent means difficulty INCREASES (higher
 * nBits). This is the OPPOSITE of BCH's target-based ASERT where a positive
 * exponent increases the target (decreasing difficulty).
 */
ASERTResult CalculateFACTORASERT(const FACTORASERTParams& params,
                                 int32_t anchorNBits,
                                 int64_t timeDiff,
                                 int64_t heightDiff);

uint16_t GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
int32_t CalculateDifficultyDelta(const int32_t nBits, const double nPeriodTimeProportionConsumed, const bool isHardDiffRemoved);
int32_t CalculateInterimDifficultyDelta(const int32_t nBits, const double nPeriodTimeProportionConsumed);
uint16_t CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&,
                                   int64_t nTargetTimespan = 0, bool isInterimDAA = false);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork( const CBlockHeader& block, const Consensus::Params&);
uint1024 gHash( const CBlockHeader& block, const Consensus::Params&);

arith_uint256 CalculateASERT(const arith_uint256 &refTarget,
                             const int64_t nPowTargetSpacing,
                             const int64_t nTimeDiff,
                             const int64_t nHeightDiff,
                             const arith_uint256 &powLimit,
                             const int64_t nHalfLife) noexcept;

uint32_t GetNextASERTWorkRequired(const CBlockIndex *pindexPrev,
                                  const CBlockHeader *pblock,
                                  const Consensus::Params &params,
                                  const CBlockIndex *pindexAnchorBlock)
                                  noexcept;

/**
 * ASERT caches a special block index for efficiency. If block indices are
 * freed then this needs to be called to ensure no dangling pointer when a new
 * block tree is created.
 * (this is temporary and will be removed after the ASERT constants are fixed)
 */
void ResetASERTAnchorBlockCache() noexcept;

bool IsASERTEnabled(const Consensus::Params &params,
                    const CBlockIndex *pindexPrev);

//Factoring pollar rho algorithm
int rho( uint64_t &g, uint64_t n);
int rho( mpz_t g, mpz_t n);

#endif // BITCOIN_POW_H
