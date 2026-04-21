#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test regtest restart with a future-tip chain.

On regtest, block generation with mocktime can intentionally produce a tip
that is far ahead of wall-clock time. Restarting should still succeed.
"""

import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MiningFutureTipTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.supports_cli = False

    def run_test(self):
        node = self.nodes[0]
        address = node.get_deterministic_priv_key().address

        self.log.info("Mine blocks with increasing mocktime to create a future tip")
        mock_time = int(time.time())
        for _ in range(40):
            node.setmocktime(mock_time)
            node.generatetoaddress(1, address)
            mock_time += 600

        assert_equal(node.getblockcount(), 40)

        self.log.info("Reset mocktime and restart node")
        node.setmocktime(0)
        self.restart_node(0)

        self.log.info("Verify the node restarted and kept the chain")
        assert_equal(self.nodes[0].getblockcount(), 40)


if __name__ == '__main__':
    MiningFutureTipTest().main()
