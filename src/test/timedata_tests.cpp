// Copyright (c) 2011-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//

#include <netaddress.h>
#include <test/util/setup_common.h>
#include <timedata.h>
#include <util/string.h>
#include <util/time.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(timedata_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(util_MedianFilter)
{
    CMedianFilter<int> filter(5, 15);

    BOOST_CHECK_EQUAL(filter.median(), 15);

    filter.input(20); // [15 20]
    BOOST_CHECK_EQUAL(filter.median(), 17);

    filter.input(30); // [15 20 30]
    BOOST_CHECK_EQUAL(filter.median(), 20);

    filter.input(3); // [3 15 20 30]
    BOOST_CHECK_EQUAL(filter.median(), 17);

    filter.input(7); // [3 7 15 20 30]
    BOOST_CHECK_EQUAL(filter.median(), 15);

    filter.input(18); // [3 7 18 20 30]
    BOOST_CHECK_EQUAL(filter.median(), 18);

    filter.input(0); // [0 3 7 18 30]
    BOOST_CHECK_EQUAL(filter.median(), 7);
}

BOOST_AUTO_TEST_CASE(addtimedata)
{
    // Peer time adjustment is disabled. GetTimeOffset() must always return 0
    // and GetAdjustedTime() must equal GetTime(), regardless of peer offsets.
    BOOST_CHECK_EQUAL(GetTimeOffset(), 0);

    CNetAddr addr;
    addr.SetInternal(ToString(1));
    AddTimeData(addr, 3600);

    BOOST_CHECK_EQUAL(GetTimeOffset(), 0);
    BOOST_CHECK_EQUAL(GetAdjustedTime(), GetTime());
}

BOOST_AUTO_TEST_SUITE_END()
