// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>
#include <stdint.h>
#include <gmp.h>
#include <gmpxx.h>

class CBlockHeader;
class CBlockIndex;
class uint256;

uint16_t GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
int32_t CalculateDifficultyDelta(const int32_t nBits, const double nPeriodTimeProportionConsumed, const bool isHardDiffRemoved);
uint16_t CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork( const CBlockHeader& block, const Consensus::Params&);
uint1024 gHash( const CBlockHeader& block, const Consensus::Params&);

//Factoring pollar rho algorithm
int rho( uint64_t &g, uint64_t n);
int rho( mpz_t g, mpz_t n);

#endif // BITCOIN_POW_H
