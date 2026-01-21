// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <deploymentinfo.h>
#include <hash.h> // for signet block challenge hash
#include <util/system.h>

#include <assert.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

static CBlock CreateGenesisBlock(
        const char* pszTimestamp, 
        const CScript& genesisOutputScript, 
        uint32_t nTime, 
        uint64_t nNonce, 
        uint16_t nBits, 
        int32_t nVersion, 
        int64_t wOffset, 
        const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.wOffset  = wOffset;
   
    //Genesis parameters for different networks on FACT0RN.
    if( nTime == 1650443545){                
        genesis.nP1      = uint1024S("0xb5ff");                            //Regtest
    }
    else if ( nTime == 1650442708 ){ 
        genesis.nP1      = uint1024S("0x166ad939aed84a268f7c2ae4f5d");     //Testnet
    } else if (nTime == 1650449340 ) {                           
        genesis.nP1      = uint1024S("0x5b541e0fc53ad9c40daa99c31c17b");   //Mainnet
    }

    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot      = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint64_t nNonce, uint16_t nBits, int32_t nVersion, int64_t wOffset, const CAmount& genesisReward)
{
    const char* pszTimestamp = "The Times 4/20/2022 Russia Strikes Hard as It Pushes to Seize Donbas Region";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, wOffset, genesisReward);
}

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        genesis = CreateGenesisBlock( 1650449340ULL , 4081969520ULL, 230, 0 , 2375LL,  0);
        consensus.hashGenesisBlock = genesis.GetHash();
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.BIP16Exception = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000") ;
        consensus.BIP34Height  = 1;
        consensus.BIP34Hash    = uint256S("");
        consensus.BIP65Height  = 1;
        consensus.BIP66Height  = 1;
        consensus.CSVHeight    = 1;
        consensus.SegwitHeight = 1;
        consensus.TaprootHeight = 1;
        consensus.MinBIP9WarningHeight = 1; // segwit activation height + miner confirmation window
        consensus.powLimit = 230;
        consensus.nPowTargetTimespan = 14ULL * 24ULL * 60ULL * 60ULL; // 14 Days * 24 Hours * 60 Minutes * 60 Seconds |-> Seconds in 2 weeks.
        consensus.nPowTargetSpacing  =   30 * 60;                     // 30 Minutes * 60 Seconds                      |-> Seconds in 30 minutes
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting  = false;
        consensus.nRuleChangeActivationThreshold = 639; // 95% of 672 (rounded up from 638.4)
        consensus.nMinerConfirmationWindow = 672; // nPowTargetTimespan / nPowTargetSpacing

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 1; // No activation delay

        consensus.nMinimumChainWork  = uint256S("0x10a8");
        consensus.defaultAssumeValid = genesis.GetHash(); 

        //Number of Miller-Rabin rounds, determines primality with false positive rate of 4^(-rounds).
        //GMP docs suggest 32 - 50 is a reasonable range for MillerRabin. We chose the high end, 50.
        consensus.MillerRabinRounds = 50;

	    //Number of rounds for gHash to generate random Ws around which to search for semiprimes.
	    consensus.hashRounds = 1;

        // Deadpool softfork
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].bit = 27;
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].nStartTime = 1735689600LL; // 2025-01-01
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].nTimeout = 1748736000LL; // 2025-06-01
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].min_activation_height = 155000; // no delay

        // Deadpool parametrization
        consensus.nDeadpoolAnnounceMaturity = 100;
        consensus.nDeadpoolAnnounceValidity = 672;
        consensus.nDeadpoolAnnounceMinBurn = 1000000; // 0.01 COIN

        // Hard diff removal hardfork
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].bit = 26;
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].nStartTime = 1743465600LL; // 2025-04-01
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].nTimeout = 1775001600LL; // 2026-04-01
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].min_activation_height = 160000; // no delay

        // Interim DAA hardfork
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].bit = 25;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nStartTime = 1767225600LL;  // 2026-01-01
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nTimeout = 1784505600LL;  // 2026-07-20
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nPeriod = Consensus::INTERIM_DAA_PERIOD;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nThreshold = Consensus::INTERIM_DAA_THRESHOLD;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].max_active_blocks = Consensus::INTERIM_DAA_MAX_ACTIVE;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xca;
        pchMessageStart[1] = 0xfe;
        pchMessageStart[2] = 0xca;
        pchMessageStart[3] = 0xfe;
        nDefaultPort = 30030;
        nPruneAfterHeight = 1;
        m_assumed_blockchain_size = 420;
        m_assumed_chain_state_size = 6;

	    //Assert for genesis block.
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("79cb40f8075b0e3dc2bc468c5ce2a7acbe0afd36c6c3d3a134ea692edac7de49"));
        assert(genesis.hashMerkleRoot == uint256S("fe56b75eb001df55cfe63e768ff54a7a376a3108119c9cedd1c6b5045649b108"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY]     = std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "fact";

        vSeeds.emplace_back("sn0.projectfactor.io");
        vSeeds.emplace_back("sn1.projectfactor.io");
        vSeeds.emplace_back("sn2.projectfactor.io");
        vSeeds.emplace_back("sn3.projectfactor.io");
        vSeeds.emplace_back("sn4.projectfactor.io");
        vSeeds.emplace_back("sn5.projectfactor.io");
        vSeeds.emplace_back("sn6.projectfactor.io");
        vSeeds.emplace_back("sn7.projectfactor.io");
        vSeeds.emplace_back("sn8.projectfactor.io");
        vSeeds.emplace_back("sn9.projectfactor.io");

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;
        m_is_mockable_chain = false;

        checkpointData = {
          {
            {0, uint256S("79cb40f8075b0e3dc2bc468c5ce2a7acbe0afd36c6c3d3a134ea692edac7de49") },
            {14000, uint256S("7da5b7fb59a8b8aa645e89f7efb154a70237ee462b91933edbd877de5bf08e92") }
          }
        };

        m_assumeutxo_data = MapAssumeutxo{
         // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            // Data from RPC: getchaintxstats 4096 7da5b7fb59a8b8aa645e89f7efb154a70237ee462b91933edbd877de5bf08e92
            /* nTime    */ 1653056471,
            /* nTxCount */ 14001,
            /* dTxRate  */ 0.00420f,
        };
    }
};

/**
 * Testnet (v3): public test network which is reset from time to time.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET;
        genesis = CreateGenesisBlock( 1650442708ULL,  4143631544ULL, 210,  0, -2813, 0);
        consensus.hashGenesisBlock = genesis.GetHash();
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.BIP16Exception = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000") ;
        consensus.BIP34Height = 1;
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.TaprootHeight = 1;
        consensus.MinBIP9WarningHeight = 1; // segwit activation height + miner confirmation window
        consensus.powLimit = 210;
        consensus.nPowTargetTimespan = 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 5 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 90; // 75% for testchains
        consensus.nMinerConfirmationWindow = 288; // nPowTargetTimespan / nPowTargetSpacing

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256S("0x10a8");
        consensus.defaultAssumeValid = genesis.GetHash(); 

        //Number of Miller-Rabin rounds, determines primality with false positive rate of 4^(-rounds).
        consensus.MillerRabinRounds = 50 ;

        // Deadpool softfork
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].bit = 27;
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].nStartTime = 1735689600LL; // Jan 1st, 2025
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].min_activation_height = 0; // No activation delay

        // Deadpool parametrization
        consensus.nDeadpoolAnnounceMaturity = 5;
        consensus.nDeadpoolAnnounceValidity = 100;
        consensus.nDeadpoolAnnounceMinBurn = 1000000; // 0.01 COIN

        // Hard diff removal hardfork
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].bit = 26;
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].min_activation_height = (4 * consensus.nMinerConfirmationWindow);

        // Interim DAA hardfork
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].bit = 25;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nStartTime = 1767225600LL;  // 2026-01-01
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nPeriod = Consensus::INTERIM_DAA_PERIOD;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nThreshold = Consensus::INTERIM_DAA_THRESHOLD;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].max_active_blocks = Consensus::INTERIM_DAA_MAX_ACTIVE;

	    //Number of rounds for gHash to generate random Ws around which to search for semiprimes.
	    consensus.hashRounds = 1;

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xc7;
        pchMessageStart[2] = 0x02;
        pchMessageStart[3] = 0x88;
        nDefaultPort = 42069;
        nPruneAfterHeight = 1;
        m_assumed_blockchain_size = 40;
        m_assumed_chain_state_size = 2;

	    //Assert for genesis block.
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("550bbf0a444d9f92189f067dd225f5b8a5d92587ebc2e8398d143236072580af"));
        assert(genesis.hashMerkleRoot == uint256S("fe56b75eb001df55cfe63e768ff54a7a376a3108119c9cedd1c6b5045649b108"));

        //Seeds
        vFixedSeeds.clear();
        vSeeds.clear();
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tfact";

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_test), std::end(chainparams_seed_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;
        m_is_mockable_chain = false;

        checkpointData = {
          {
            {0, uint256S("550bbf0a444d9f92189f067dd225f5b8a5d92587ebc2e8398d143236072580af") },
            {3990, uint256S("ecb678bcb76dfe655b69ff3b9094be33c6f3dac118fe58bd7dff57e62e28c7c2") }
          }
        };

        m_assumeutxo_data = MapAssumeutxo{
            // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            // Data from RPC: getchaintxstats 2048 ecb678bcb76dfe655b69ff3b9094be33c6f3dac118fe58bd7dff57e62e28c7c2
            /* nTime    */ 1653331887,
            /* nTxCount */ 3991,
            /* dTxRate  */ 0.00135f,
        };
    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const ArgsManager& args) {
        std::vector<uint8_t> bin;
        vSeeds.clear();

        strNetworkID = CBaseChainParams::SIGNET;
        genesis = CreateGenesisBlock(1640995299, 52613770, 33, 1, 0, 0);
        consensus.hashGenesisBlock = genesis.GetHash();

        if (!args.IsArgSet("-signetchallenge")) {
            bin = ParseHex("512103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae");
            //vSeeds.emplace_back("178.128.221.177");
            //vSeeds.emplace_back("2a01:7c8:d005:390::5");
            vSeeds.emplace_back("v7ajjeirttkbnt32wpy3c6w3emwnfr3fkla7hpxcfokr3ysd3kqtzmqd.onion:38333");

            consensus.nMinimumChainWork = uint256S("0x10a8");
            consensus.defaultAssumeValid = genesis.GetHash(); 
            m_assumed_blockchain_size = 1;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                // Data from RPC: getchaintxstats 4096 000000187d4440e5bff91488b700a140441e089a8aaea707414982460edbfe54
                /* nTime    */ 1626696658,
                /* nTxCount */ 387761,
                /* dTxRate  */ 0.04035946932424404,
            };
        } else {
            const auto signet_challenge = args.GetArgs("-signetchallenge");
            if (signet_challenge.size() != 1) {
                throw std::runtime_error(strprintf("%s: -signetchallenge cannot be multiple values.", __func__));
            }
            bin = ParseHex(signet_challenge[0]);

            consensus.nMinimumChainWork = uint256S("0x10a8");
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };
            LogPrintf("Signet with challenge %s\n", signet_challenge[0]);
        }

        if (args.IsArgSet("-signetseednode")) {
            vSeeds = args.GetArgs("-signetseednode");
        }


        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.BIP16Exception = uint256{};
        consensus.BIP34Height = 1;
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.TaprootHeight = 1;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 30 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 672; // nPowTargetTimespan / nPowTargetSpacing
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = 32;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // message start is defined as the first 4 bytes of the sha256d of the block script
        CHashWriter h(SER_DISK, 0);
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        memcpy(pchMessageStart, hash.begin(), 4);

        nDefaultPort = 38333;
        nPruneAfterHeight = 1000;

        //Number of Miller-Rabin rounds, determines primality with false positive rate of 4^(-rounds).
        consensus.MillerRabinRounds = 50;

	    //Number of rounds for gHash to generate random Ws around which to search for semiprimes.
	    consensus.hashRounds = 1;

        // Deadpool softfork
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].bit = 27;
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].min_activation_height = (4 * consensus.nMinerConfirmationWindow); // Add one more epoch than required

        // Deadpool parametrization
        consensus.nDeadpoolAnnounceMaturity = 5;
        consensus.nDeadpoolAnnounceValidity = 100;
        consensus.nDeadpoolAnnounceMinBurn = 1000000; // 0.01 COIN

        // Hard diff removal hardfork
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].bit = 26;
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].nStartTime = 1743465600LL; // 2025-04-01
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].nTimeout = 1775001600LL; // 2026-04-01
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].min_activation_height = 160000; // no delay

        // Interim DAA hardfork
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].bit = 25;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nStartTime = 1767225600LL;  // 2026-01-01
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nPeriod = Consensus::INTERIM_DAA_PERIOD;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nThreshold = Consensus::INTERIM_DAA_THRESHOLD;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].max_active_blocks = Consensus::INTERIM_DAA_MAX_ACTIVE;

        vFixedSeeds.clear();
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = true;
        m_is_mockable_chain = false;
    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams {
public:
    explicit CRegTestParams(const ArgsManager& args) {
        strNetworkID =  CBaseChainParams::REGTEST;
        genesis = CreateGenesisBlock( 1650443545ULL, 2706135317ULL, 32, 0, 254, 0);
        consensus.hashGenesisBlock = genesis.GetHash();
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height =1;
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.TaprootHeight = 1;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = 32;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 30 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 24; // 75% for testchains
        consensus.nMinerConfirmationWindow = 32; // Faster than normal for regtest (32 instead of 2016)

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256S("0x10a8");
        consensus.defaultAssumeValid = uint256{};

        pchMessageStart[0] = 0xbe;
        pchMessageStart[1] = 0xdb;
        pchMessageStart[2] = 0xed;
        pchMessageStart[3] = 0xbe;
        nDefaultPort = 18444;
        nPruneAfterHeight = args.GetBoolArg("-fastprune", false) ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        //Number of Miller-Rabin rounds, determines primality with false positive rate of 4^(-rounds).
        consensus.MillerRabinRounds = 50;

	    //Number of rounds for gHash to generate random Ws around which to search for semiprimes.
	    consensus.hashRounds = 1;

        // Deadpool softfork
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].bit = 27;
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_DEADPOOL].min_activation_height = (4 * consensus.nMinerConfirmationWindow); // Add one more epoch than required

        // Deadpool parametrization
        consensus.nDeadpoolAnnounceMaturity = 5;
        consensus.nDeadpoolAnnounceValidity = 100;
        consensus.nDeadpoolAnnounceMinBurn = 1000000; // 0.01 COIN

        // Hard diff removal hardfork
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].bit = 26;
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].nStartTime = 1743465600LL; // 2025-04-01
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].nTimeout = 1775001600LL; // 2026-04-01
        consensus.vDeployments[Consensus::DEPLOYMENT_HARD_DIFF_REMOVAL].min_activation_height = 160000; // no delay

        // Interim DAA hardfork
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].bit = 25;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nStartTime = 1767225600LL;  // 2026-01-01
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nPeriod = Consensus::INTERIM_DAA_PERIOD;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].nThreshold = Consensus::INTERIM_DAA_THRESHOLD;
        consensus.vDeployments[Consensus::DEPLOYMENT_INTERIM_DAA].max_active_blocks = Consensus::INTERIM_DAA_MAX_ACTIVE;

        UpdateActivationParametersFromArgs(args);

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = true;
        m_is_mockable_chain = true;

	    //Assert for genesis block.
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("38039464f800f026086985e81e6af3ceb35c2b93f042d79ab637d692eb002136"));
        assert(genesis.hashMerkleRoot == uint256S("fe56b75eb001df55cfe63e768ff54a7a376a3108119c9cedd1c6b5045649b108"));
        

        checkpointData = {
            {
                {0, uint256S("38039464f800f026086985e81e6af3ceb35c2b93f042d79ab637d692eb002136")},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "bcrt";
    }

    /**
     * Allows modifying the Version Bits regtest parameters.
     */
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int min_activation_height)
    {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
        consensus.vDeployments[d].min_activation_height = min_activation_height;
    }
    void UpdateActivationParametersFromArgs(const ArgsManager& args);
};

void CRegTestParams::UpdateActivationParametersFromArgs(const ArgsManager& args)
{
    if (args.IsArgSet("-segwitheight")) {
        int64_t height = args.GetArg("-segwitheight", consensus.SegwitHeight);
        if (height < -1 || height >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Activation height %ld for segwit is out of valid range. Use -1 to disable segwit.", height));
        } else if (height == -1) {
            LogPrintf("Segwit disabled for testing\n");
            height = std::numeric_limits<int>::max();
        }
        consensus.SegwitHeight = static_cast<int>(height);
    }

    if (!args.IsArgSet("-vbparams")) return;

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams;
        boost::split(vDeploymentParams, strDeployment, boost::is_any_of(":"));
        if (vDeploymentParams.size() < 3 || 4 < vDeploymentParams.size()) {
            throw std::runtime_error("Version bits parameters malformed, expecting deployment:start:end[:min_activation_height]");
        }
        int64_t nStartTime, nTimeout;
        int min_activation_height = 0;
        if (!ParseInt64(vDeploymentParams[1], &nStartTime)) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        if (!ParseInt64(vDeploymentParams[2], &nTimeout)) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        if (vDeploymentParams.size() >= 4 && !ParseInt32(vDeploymentParams[3], &min_activation_height)) {
            throw std::runtime_error(strprintf("Invalid min_activation_height (%s)", vDeploymentParams[3]));
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout, min_activation_height);
                found = true;
                LogPrintf("Setting version bits activation parameters for %s to start=%ld, timeout=%ld, min_activation_height=%d\n", vDeploymentParams[0], nStartTime, nTimeout, min_activation_height);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager& args, const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN) {
        return std::unique_ptr<CChainParams>(new CMainParams());
    } else if (chain == CBaseChainParams::TESTNET) {
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    } else if (chain == CBaseChainParams::SIGNET) {
        return std::unique_ptr<CChainParams>(new SigNetParams(args));
    } else if (chain == CBaseChainParams::REGTEST) {
        return std::unique_ptr<CChainParams>(new CRegTestParams(args));
    }
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(gArgs, network);
}
