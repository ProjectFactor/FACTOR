#!/usr/bin/env python3
# Copyright (c) 2024 The FACT0RN developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test interim DAA hardfork activation, behavior, and reversion.

This test builds a single continuous chain to verify:
- Phase 1: BIP9 state machine (DEFINED -> STARTED -> LOCKED_IN -> ACTIVE)
- Phase 2: Active period (DAA bands, difficulty adjustments)
- Phase 3: Reversion (auto-revert after max_active_blocks)
"""

from test_framework.blocktools import (
    NORMAL_GBT_REQUEST_PARAMS,
    create_block,
)
from test_framework.messages import msg_block
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

# Interim DAA parameters (from consensus/params.h)
INTERIM_DAA_PERIOD = 42
INTERIM_DAA_THRESHOLD = 40
INTERIM_DAA_MAX_ACTIVE = 1344
INTERIM_DAA_BIT = 25

# Version bits
VERSIONBITS_TOP_BITS = 0x20000000
INTERIM_DAA_VERSION = VERSIONBITS_TOP_BITS | (1 << INTERIM_DAA_BIT)
NO_SIGNAL_VERSION = VERSIONBITS_TOP_BITS

# Target block time in seconds (30 minutes)
TARGET_BLOCK_TIME = 30 * 60


class InterimDAATest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

        # Prevent `[net] [net.cpp:1336] [InactivityCheck] socket receive timeout: 117243310s peer=0`
        # Somehow node freezes with 9223372036854775807, so we use 2^62
        self.extra_args = [[f"-peertimeout={2**62}"]]

    def setup_network(self):
        self.setup_nodes()

        # Instance variables to track chain state across tests
        self.activation_height = None
        self.last_active_height = None
        self.first_reverted_height = None

    # =========================================================================
    # Helper Methods
    # =========================================================================

    def send_blocks(
        self,
        peer,
        node,
        numblocks: int,
        version: int,
        time_per_block: int = TARGET_BLOCK_TIME,
    ) -> list[str]:
        """
        Send numblocks to peer with specified version and timing.

        Uses getblocktemplate to obtain the correct nBits value for each block,
        Uses setmocktime to control the node's time perception for DAA testing.

        Args:
            peer: P2P connection
            node: Node RPC interface
            numblocks: Number of blocks to send
            version: Block version (for signaling)
            time_per_block: Seconds between blocks (for DAA testing)

        Returns:
            List of block hashes created
        """
        tip_header = node.getblockheader(node.getbestblockhash())
        block_time = tip_header["time"] + time_per_block
        hashes = []
        expected_height = node.getblockcount() + 1

        for _ in range(numblocks):
            # Set mocktime so getblocktemplate returns correct curtime and bits
            node.setmocktime(block_time)

            # Get block template with correct nBits from the node
            tmpl = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
            tmpl_bits = tmpl["bits"]
            tmpl_height = tmpl["height"]

            # Create block using template
            block = create_block(tmpl=tmpl, ntime=block_time)
            block.nVersion = version
            block.solve()

            # Log for progress tracking
            if expected_height % INTERIM_DAA_PERIOD == 0:
                self.log.info(
                    f"DAA boundary block {expected_height}: nBits={block.nBits}"
                )

            peer.send_message(msg_block(block))

            # Wait for the block to be connected to the chain, not just received.
            # sync_with_ping() only waits for ping/pong, but the block connection
            # might still be pending. We need to wait for the height to increase.
            try:
                self.wait_until(
                    lambda: node.getblockcount() >= expected_height, timeout=10
                )
            except AssertionError:
                self.log.error(
                    f"Block {expected_height} not accepted. Template bits={tmpl_bits}, height={tmpl_height}"
                )
                raise

            block_time += time_per_block
            hashes.append(block.sha256)
            expected_height += 1

        # Clear mocktime after mining
        node.setmocktime(0)
        return hashes

    def get_softfork_status(self, node, deployment: str = "interim_daa"):
        """Get the status of a softfork deployment."""
        info = node.getblockchaininfo()
        return info["softforks"].get(deployment, {})

    def check_interim_daa_status(self, node, expected_status, expected_active):
        """Verify interim_daa deployment status."""
        sf = self.get_softfork_status(node)
        assert_equal(sf["type"], "bip9")
        assert_equal(sf["bip9"]["status"], expected_status)
        assert_equal(sf["active"], expected_active)
        return sf

    def get_next_block_bits(self, node):
        """Get nBits from a block."""
        tmpl = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        return tmpl["bits"]

    # =========================================================================
    # Phase 1: BIP9 State Machine Tests (blocks 0-335)
    # =========================================================================

    def test_defined_state(self, node, peer):
        """Verify DEFINED -> STARTED transition at block 84."""
        self.log.info("=== Phase 1a: Testing DEFINED -> STARTED transition ===")

        # Verify initial state is DEFINED
        self.check_interim_daa_status(node, "defined", False)
        self.log.info("Initial state: DEFINED")

        # Mine through DEFINED period
        self.log.info(f"Mining {INTERIM_DAA_PERIOD} blocks to exit DEFINED state...")
        self.send_blocks(peer, node, INTERIM_DAA_PERIOD, NO_SIGNAL_VERSION)

        # Verify transition to STARTED
        self.check_interim_daa_status(node, "started", False)
        assert_equal(node.getblockcount(), INTERIM_DAA_PERIOD)
        self.log.info(f"Transitioned to STARTED at height {node.getblockcount()}")

    def test_threshold_failure(self, node, peer):
        """Verify 39/42 signals does NOT trigger LOCKED_IN (stays STARTED)."""
        self.log.info("=== Phase 1b: Testing threshold failure (39/42) ===")

        # Mine 39 signaling blocks (one below threshold)
        below_threshold = INTERIM_DAA_THRESHOLD - 1
        self.log.info(f"Mining {below_threshold} signaling blocks (below threshold)...")
        self.send_blocks(peer, node, below_threshold, INTERIM_DAA_VERSION)

        # Mine remaining non-signaling blocks to complete period
        remaining = INTERIM_DAA_PERIOD - below_threshold
        self.log.info(f"Mining {remaining} non-signaling blocks...")
        self.send_blocks(peer, node, remaining, NO_SIGNAL_VERSION)

        # Verify still in STARTED (threshold not met)
        self.check_interim_daa_status(node, "started", False)
        assert_equal(node.getblockcount(), INTERIM_DAA_PERIOD * 2)
        self.log.info(
            f"{below_threshold}/{INTERIM_DAA_PERIOD} signals: stays STARTED at height {node.getblockcount()}"
        )

    def test_threshold_success(self, node, peer):
        """Verify 40/42 signals triggers LOCKED_IN."""
        self.log.info("=== Phase 1c: Testing threshold success (40/42) ===")

        # Mine exactly threshold signaling blocks
        self.log.info(f"Mining {INTERIM_DAA_THRESHOLD} signaling blocks...")
        self.send_blocks(peer, node, INTERIM_DAA_THRESHOLD, INTERIM_DAA_VERSION)

        # Mine remaining non-signaling blocks
        remaining = INTERIM_DAA_PERIOD - INTERIM_DAA_THRESHOLD
        self.log.info(f"Mining {remaining} non-signaling blocks...")
        self.send_blocks(peer, node, remaining, NO_SIGNAL_VERSION)

        # Verify LOCKED_IN
        self.check_interim_daa_status(node, "locked_in", False)
        locked_in_height = node.getblockcount()
        assert_equal(locked_in_height, INTERIM_DAA_PERIOD * 3)
        self.log.info(
            f"{INTERIM_DAA_THRESHOLD}/{INTERIM_DAA_PERIOD} signals: LOCKED_IN at height {locked_in_height}"
        )

    def test_locked_in_to_active(self, node, peer):
        """Verify LOCKED_IN -> ACTIVE transition after one period.

        This test also verifies that when interim DAA activates at a height that is
        also a 42-block adjustment boundary, the difficulty adjustment uses
        the interim DAA formula (delta = +6 for fast blocks), not normal DAA
        (which would give delta = +4 for the same timing).
        """
        self.log.info(
            "=== Phase 1d: Testing LOCKED_IN -> ACTIVE transition behavior ==="
        )

        bits_before_activation = self.get_next_block_bits(node)
        locked_in_height = node.getblockcount()

        # Mine one more period to activate
        self.log.info(f"Mining {INTERIM_DAA_PERIOD} blocks to activate...")
        self.send_blocks(peer, node, INTERIM_DAA_PERIOD, NO_SIGNAL_VERSION, 1)

        # Verify ACTIVE
        sf = self.check_interim_daa_status(node, "active", True)
        self.activation_height = sf["bip9"]["since"]

        # Calculate and store reversion heights
        self.last_active_height = self.activation_height + INTERIM_DAA_MAX_ACTIVE - 1
        self.first_reverted_height = self.last_active_height + 1

        # Verify activation height matches expectation
        expected_activation = locked_in_height + INTERIM_DAA_PERIOD
        assert_equal(self.activation_height, expected_activation)
        assert_equal(node.getblockcount(), INTERIM_DAA_PERIOD * 4)

        # Verify difficulty adjustment is correct
        bits_after_activation = self.get_next_block_bits(node)
        bits_delta = bits_after_activation - bits_before_activation
        assert_equal(bits_delta, 6)

        self.log.info(
            f"ACTIVE at height {self.activation_height}, "
            f"last_active={self.last_active_height}, first_reverted={self.first_reverted_height}, bits_delta={bits_delta}"
        )

    # =========================================================================
    # Phase 2: Active Period Tests
    # =========================================================================

    def test_daa_all_time_bands(self, node, peer):
        """Test DAA behavior across all time proportion bands."""
        self.log.info("=== Phase 2b: Testing all DAA time bands ===")

        target_timespan = INTERIM_DAA_PERIOD * TARGET_BLOCK_TIME

        # Test parameters: (description, time_per_block, expected_delta)
        test_cases = [
            # Normal case
            ("Normal (27-31min avg)", 30 * 60, 0),
            # Small adjustments
            ("Slightly fast (20-27min avg)", 27 * 60 - 1, 2),
            ("Slightly slow (31-45min avg)", 31 * 60 + 1, -2),
            # Moderate adjustments
            ("Moderately fast (15-20min avg)", 20 * 60 - 1, 4),
            ("Moderately slow (45-60min avg)", 45 * 60 + 1, -4),
            # Large adjustments
            ("Very fast (<15min avg)", 15 * 60 - 1, 6),
            ("Very slow (>60min avg)", 60 * 60 + 1, -6),
        ]

        for desc, time_per_block, expected_delta in test_cases:
            self.log.info(f"--- {desc} ---")

            bits_before = self.get_next_block_bits(node)
            height_before = node.getblockcount()
            self.log.info(f"  nBits before: {bits_before}, height: {height_before}")

            # Mine a full period with this timing
            self.send_blocks(
                peer, node, INTERIM_DAA_PERIOD, NO_SIGNAL_VERSION, time_per_block
            )

            bits_after = self.get_next_block_bits(node)
            actual_delta = bits_after - bits_before

            self.log.info(
                f"  nBits after: {bits_after}, delta: {actual_delta}, expected: {expected_delta}"
            )

            assert_equal(actual_delta, expected_delta)
            self.log.info(f"  Passed: {desc}")

    # =========================================================================
    # Phase 3: Reversion Tests
    # =========================================================================

    def test_reversion_and_normal_daa(self, node, peer):
        """Verify reversion and that normal DAA is restored after expiration.

        This test:
        1. Identifies the 672-block window containing the reversion point
        2. Records nBits at the start of this window
        3. Mines through with one absurdly long block to skew the time proportion
        4. Verifies the delta matches normal DAA (-4 or -3), not interim DAA (-6)

        The absurdly long block makes proportion > 2.0, placed at the second-to-last
        interim DAA block since the last block's time is ignored by the DAA calculation.
        """
        self.log.info("=== Phase 3: Testing reversion and normal DAA behavior ===")

        current_height = node.getblockcount()
        normal_interval = 672
        normal_time = TARGET_BLOCK_TIME
        absurd_time = (
            normal_interval * TARGET_BLOCK_TIME * 3
        )  # 3x entire target timespan

        # =====================================================================
        # Step 1: Calculate the 672-block window containing reversion
        # =====================================================================
        window_end = self.first_reverted_height + (
            normal_interval - (self.first_reverted_height % normal_interval)
        )
        if self.first_reverted_height % normal_interval == 0:
            window_end = self.first_reverted_height
        window_start = window_end - normal_interval

        self.log.info(f"672-block window: {window_start} to {window_end}")
        self.log.info(f"Reversion at: {self.first_reverted_height}")

        if self.first_reverted_height == window_end:
            raise AssertionError(
                f"Reversion at height {self.first_reverted_height} coincides with "
                f"672-block boundary. Cannot test post-reversion DAA behavior."
            )

        # =====================================================================
        # Step 2: Mine to window start and record initial nBits
        # =====================================================================
        if current_height < window_start:
            blocks_to_window = window_start - current_height
            self.log.info(
                f"Mining {blocks_to_window} blocks to window start at {window_start}..."
            )
            self.send_blocks(peer, node, blocks_to_window, NO_SIGNAL_VERSION)

        bits_at_window_start = self.get_next_block_bits(node)
        height_at_window_start = node.getblockcount()
        assert_equal(height_at_window_start, window_start)
        self.log.info(
            f"At window start: height={height_at_window_start}, nBits={bits_at_window_start}"
        )

        # =====================================================================
        # Step 3: Mine to one block before the absurd timing block
        # The absurd block is placed at last_active_height - 1 (second-to-last
        # interim DAA block) since the last block's time is ignored by DAA.
        # =====================================================================
        absurd_block_height = self.last_active_height - 1
        blocks_before_absurd = (absurd_block_height - 1) - height_at_window_start
        if blocks_before_absurd > 0:
            self.log.info(f"Mining {blocks_before_absurd} blocks with normal timing...")
            self.send_blocks(
                peer, node, blocks_before_absurd, NO_SIGNAL_VERSION, normal_time
            )

        # =====================================================================
        # Step 4: Mine one block with absurd timing to skew the time proportion
        # =====================================================================
        self.log.info(f"Mining block {absurd_block_height} with absurd timing...")
        self.send_blocks(peer, node, 1, NO_SIGNAL_VERSION, absurd_time)
        assert_equal(node.getblockcount(), absurd_block_height)

        # =====================================================================
        # Step 5: Mine remaining blocks to window end (crosses reversion)
        # =====================================================================
        blocks_to_end = window_end - node.getblockcount()
        self.log.info(
            f"Mining {blocks_to_end} blocks to window end (crosses reversion at {self.first_reverted_height})..."
        )
        self.send_blocks(peer, node, blocks_to_end, NO_SIGNAL_VERSION, normal_time)
        assert_equal(node.getblockcount(), window_end)

        # BIP9 still shows 'active' (permanent state), but time-limited check now returns false
        sf = self.get_softfork_status(node)
        assert_equal(sf["bip9"]["status"], "active")

        # =====================================================================
        # Step 6: Verify normal DAA formula was used
        # =====================================================================
        bits_at_window_end = self.get_next_block_bits(node)
        total_delta = bits_at_window_end - bits_at_window_start

        # Normal DAA: -4 (even nBits) or -3 (odd nBits) for proportion > 2.0
        # Interim DAA would give -6 or -7 for the same proportion
        expected_delta = -4 if (bits_at_window_start % 2 == 0) else -3

        self.log.info(f"At window end: height={window_end}, nBits={bits_at_window_end}")
        self.log.info(f"Total delta: {total_delta}, expected: {expected_delta}")

        assert_equal(total_delta, expected_delta)
        self.log.info("Reversion verified: normal DAA formula used")

    # =========================================================================
    # Main Test Runner
    # =========================================================================

    def run_test(self):
        """Main test runner - builds single continuous chain."""
        node = self.nodes[0]
        peer = node.add_p2p_connection(P2PInterface())

        self.log.info("=" * 60)
        self.log.info("Starting interim DAA test - single continuous chain")
        self.log.info("=" * 60)

        # Phase 1: BIP9 State Machine
        self.test_defined_state(node, peer)
        self.test_threshold_failure(node, peer)
        self.test_threshold_success(node, peer)
        self.test_locked_in_to_active(node, peer)

        # Phase 2: Active Period
        self.test_daa_all_time_bands(node, peer)

        # Phase 3: Reversion and normal DAA verification
        self.test_reversion_and_normal_daa(node, peer)

        self.log.info("=" * 60)
        self.log.info(
            f"All interim DAA tests passed! Final height: {node.getblockcount()}"
        )
        self.log.info("=" * 60)


if __name__ == "__main__":
    InterimDAATest().main()
