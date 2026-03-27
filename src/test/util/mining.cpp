// Copyright (c) 2019-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/mining.h>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <key_io.h>
#include <miner.h>
#include <node/context.h>
#include <pow.h>
#include <random.h>
#include <script/standard.h>
#include <test/util/script.h>
#include <util/check.h>
#include <validation.h>
#include <versionbits.h>

#include <gmp.h>

void SolveBlock(CBlock& block, const Consensus::Params& params)
{
    assert(block.nBits > 0 && block.nBits <= 64);

    const uint64_t small_primes = 3ULL*5*7*11*13*17*19*23*29*31*37*41*43*47;
    const int64_t tilden = 16LL * block.nBits;
    const int expected_factor_bits = (block.nBits >> 1) + (block.nBits & 1);

    mpz_t n, g, gcd_val;
    mpz_inits(n, g, gcd_val, NULL);

    bool solved = false;
    while (!solved) {
        block.nNonce = GetRand(std::numeric_limits<uint64_t>::max());
        block.nP1.SetNull();
        block.wOffset = 0;

        uint1024 w = gHash(block, params);
        uint64_t W = ((uint64_t*)w.u8_begin())[0];
        uint64_t one = (W & 1) ? 0 : 1;

        mpz_set_ui(n, W);
        if ((int)mpz_sizeinbase(n, 2) != block.nBits)
            continue;

        for (int jj = 0; jj < tilden && !solved; jj += 2) {
            uint64_t candidates[2] = { W + jj + one, W - jj - one };
            int64_t  offsets[2]    = { (int64_t)(jj + one), -(int64_t)(jj + one) };

            for (int k = 0; k < 2 && !solved; k++) {
                uint64_t N = candidates[k];
                mpz_set_ui(n, N);
                mpz_gcd_ui(gcd_val, n, small_primes);
                if (mpz_cmp_ui(gcd_val, 1) != 0)
                    continue;

                int valid = rho(g, n);
                int bitsize = (int)mpz_sizeinbase(g, 2);

                if (valid == 1 && bitsize == expected_factor_bits) {
                    uint64_t f1 = mpz_get_ui(g);
                    assert(N % f1 == 0);
                    uint64_t f2 = N / f1;
                    uint64_t factor = std::min(f1, f2);

                    // Cofactor must have same number of bits as factor
                    if (!(f2 & (1ULL << (bitsize - 1))))
                        continue;
                    // Reject power-of-two edge case (not prime)
                    if (factor == (1ULL << (expected_factor_bits - 1)))
                        continue;

                    uint64_t* data = (uint64_t*)block.nP1.u8_begin_write();
                    data[0] = factor;
                    block.wOffset = offsets[k];
                    solved = true;
                }
            }
        }
    }

    mpz_clears(n, g, gcd_val, NULL);
    assert(CheckProofOfWork(block, params));
}

CTxIn generatetoaddress(const NodeContext& node, const std::string& address)
{
    const auto dest = DecodeDestination(address);
    assert(IsValidDestination(dest));
    const auto coinbase_script = GetScriptForDestination(dest);

    return MineBlock(node, coinbase_script);
}

std::vector<std::shared_ptr<CBlock>> CreateBlockChain(size_t total_height, const CChainParams& params)
{
    std::vector<std::shared_ptr<CBlock>> ret{total_height};
    auto time{params.GenesisBlock().nTime};
    for (size_t height{0}; height < total_height; ++height) {
        CBlock& block{*(ret.at(height) = std::make_shared<CBlock>())};

        CMutableTransaction coinbase_tx;
        coinbase_tx.vin.resize(1);
        coinbase_tx.vin[0].prevout.SetNull();
        coinbase_tx.vout.resize(1);
        coinbase_tx.vout[0].scriptPubKey = P2WSH_OP_TRUE;
        //coinbase_tx.vout[0].nValue = GetBlockSubsidy(height + 1, params.GetConsensus()); FACTOR_TODO: Update entire function.
        coinbase_tx.vin[0].scriptSig = CScript() << (height + 1) << OP_0;
        block.vtx = {MakeTransactionRef(std::move(coinbase_tx))};

        block.nVersion = VERSIONBITS_LAST_OLD_BLOCK_VERSION;
        block.hashPrevBlock = (height >= 1 ? *ret.at(height - 1) : params.GenesisBlock()).GetHash();
        block.hashMerkleRoot = BlockMerkleRoot(block);
        block.nTime = ++time;
        block.nBits = params.GenesisBlock().nBits;
        block.nNonce = 0;

        SolveBlock(block, params.GetConsensus());
    }
    return ret;
}

CTxIn MineBlock(const NodeContext& node, const CScript& coinbase_scriptPubKey)
{
    auto block = PrepareBlock(node, coinbase_scriptPubKey);

    SolveBlock(*block, Params().GetConsensus());

    bool processed{Assert(node.chainman)->ProcessNewBlock(Params(), block, true, nullptr)};
    assert(processed);

    return CTxIn{block->vtx[0]->GetHash(), 0};
}

std::shared_ptr<CBlock> PrepareBlock(const NodeContext& node, const CScript& coinbase_scriptPubKey)
{
    auto block = std::make_shared<CBlock>(
        BlockAssembler{Assert(node.chainman)->ActiveChainstate(), *Assert(node.mempool), Params()}
            .CreateNewBlock(coinbase_scriptPubKey)
            ->block);

    LOCK(cs_main);
    block->nTime = Assert(node.chainman)->ActiveChain().Tip()->GetBlockTime() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    return block;
}
