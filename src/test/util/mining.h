// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_UTIL_MINING_H
#define BITCOIN_TEST_UTIL_MINING_H

#include <memory>
#include <string>
#include <vector>

class CBlock;
class CChainParams;
class CScript;
class CTxIn;
struct NodeContext;
namespace Consensus { struct Params; }

/** Solve a block's PoW by finding a valid semiprime factorization via Pollard's rho.
 *  Sets block.nNonce, block.nP1, and block.wOffset. Requires nBits <= 64. */
void SolveBlock(CBlock& block, const Consensus::Params& params);

/** Create a blockchain, starting from genesis */
std::vector<std::shared_ptr<CBlock>> CreateBlockChain(size_t total_height, const CChainParams& params);

/** Returns the generated coin */
CTxIn MineBlock(const NodeContext&, const CScript& coinbase_scriptPubKey);

/** Prepare a block to be mined */
std::shared_ptr<CBlock> PrepareBlock(const NodeContext&, const CScript& coinbase_scriptPubKey);

/** RPC-like helper function, returns the generated coin */
CTxIn generatetoaddress(const NodeContext&, const std::string& address);

#endif // BITCOIN_TEST_UTIL_MINING_H
