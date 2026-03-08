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
