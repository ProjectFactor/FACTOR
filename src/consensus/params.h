// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <uint256.h>
#include <limits>

namespace Consensus {

/**
 * A buried deployment is one where the height of the activation has been hardcoded into
 * the client implementation long after the consensus change has activated. See BIP 90.
 */
enum BuriedDeployment : int16_t {
    // buried deployments get negative values to avoid overlap with DeploymentPos
    DEPLOYMENT_HEIGHTINCB = std::numeric_limits<int16_t>::min(),
    DEPLOYMENT_CLTV,
    DEPLOYMENT_DERSIG,
    DEPLOYMENT_CSV,
    DEPLOYMENT_SEGWIT,
    DEPLOYMENT_TAPROOT,
    DEPLOYMENT_DEADPOOL,
    DEPLOYMENT_HARD_DIFF_REMOVAL,
};
constexpr bool ValidDeployment(BuriedDeployment dep) { return dep <= DEPLOYMENT_HARD_DIFF_REMOVAL; }

enum DeploymentPos : uint16_t {
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_INTERIM_DAA,
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in deploymentinfo.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};
constexpr bool ValidDeployment(DeploymentPos dep) { return dep < MAX_VERSION_BITS_DEPLOYMENTS; }

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;
    /** If lock in occurs, delay activation until at least this block
     *  height.  Note that activation will only occur on a retarget
     *  boundary.
     */
    int min_activation_height{0};
    /** Per-deployment voting period override. 0 = use global nMinerConfirmationWindow */
    int nPeriod{0};
    /** Per-deployment threshold override. 0 = use global nRuleChangeActivationThreshold */
    int nThreshold{0};
    /** Auto-deactivate after this many blocks post-activation. 0 = permanent */
    int max_active_blocks{0};

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;

    /** Special value for nStartTime indicating that the deployment is never active.
     *  This is useful for integrating the code changes for a new feature
     *  prior to deploying it on some or all networks. */
    static constexpr int64_t NEVER_ACTIVE = -2;
};

/** Constants for interim DAA deployment */
static constexpr int INTERIM_DAA_PERIOD = 42;
static constexpr int INTERIM_DAA_THRESHOLD = 40;       // 95% of 42
static constexpr int INTERIM_DAA_MAX_ACTIVE = 1344;    // 2 normal epochs

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    /* Block hash that is excepted from BIP16 enforcement */
    uint256 BIP16Exception;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    int TaprootHeight;
    /** Block height at which Deadpool becomes active */
    int DeadpoolHeight;
    /** Block height at which Hard Diff Removal becomes active */
    int HardDiffRemovalHeight;
    /** Don't warn about unknown BIP 9 activations below this height.
     * This prevents us from warning about the CSV and segwit activations. */
    int MinBIP9WarningHeight;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint16_t powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    double maxLilt; // The extrema of timespan deviation
    int32_t maxRetargetDelta; // The extrema of the retargeting delta
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    /** The best chain should have at least this much work */
    uint256 nMinimumChainWork;
    /** By default assume that the signatures in ancestors of this block are valid */
    uint256 defaultAssumeValid;

    //Number of Miller-Rabin rounds, determines primality with false positive rate of 4^(-rounds).
    uint16_t MillerRabinRounds;

    //Number of rounds for gHash to generate random Ws around which to search for semiprimes.
    uint32_t hashRounds;

    //// deadpool parameters ////

    //! The number of blocks a deadpool announcement needs to mature
    uint16_t nDeadpoolAnnounceMaturity;
    //! The number of blocks a deadpool announcement is valid for
    uint16_t nDeadpoolAnnounceValidity;
    //! The minimum amount to burn on an announcement
    int64_t nDeadpoolAnnounceMinBurn;

    int64_t DeadpoolAnnounceMaxAge() const { return nDeadpoolAnnounceMaturity + nDeadpoolAnnounceValidity; }

    /**
     * If true, witness commitments contain a payload equal to a Bitcoin Script solution
     * to the signet challenge. See BIP325.
     */
    bool signet_blocks{false};
    std::vector<uint8_t> signet_challenge;

    int DeploymentHeight(BuriedDeployment dep) const
    {
        switch (dep) {
        case DEPLOYMENT_HEIGHTINCB:
            return BIP34Height;
        case DEPLOYMENT_CLTV:
            return BIP65Height;
        case DEPLOYMENT_DERSIG:
            return BIP66Height;
        case DEPLOYMENT_CSV:
            return CSVHeight;
        case DEPLOYMENT_SEGWIT:
            return SegwitHeight;
        case DEPLOYMENT_TAPROOT:
            return TaprootHeight;
        case DEPLOYMENT_DEADPOOL:
            return DeadpoolHeight;
        case DEPLOYMENT_HARD_DIFF_REMOVAL:
            return HardDiffRemovalHeight;
        } // no default case, so the compiler can warn about missing cases
        return std::numeric_limits<int>::max();
    }
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
