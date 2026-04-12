// Copyright (c) 2014-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <timedata.h>

#include <logging.h>
#include <netaddress.h>
#include <util/time.h>

int64_t GetTimeOffset()
{
    return 0;
}

int64_t GetAdjustedTime()
{
    return GetTime();
}

void AddTimeData(const CNetAddr& ip, int64_t nOffsetSample)
{
    if (LogAcceptCategory(BCLog::NET)) {
        LogPrint(BCLog::NET, "peer time offset %+d (%+d minutes) - ignored, using local clock only\n", nOffsetSample, nOffsetSample / 60);
    }
}
