// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos/cache.h>

#include <chainparams.h>
#include <pos/kernel.h>
#include <timedata.h>
#include <util/system.h>
#include <validation.h>

int cacheHit, cacheMiss, cacheLastCleared;
std::unordered_map<unsigned int, uint64_t> cachedModifiers;

void InitSmartstakeCache()
{
    cacheHit = 0;
    cacheMiss = 0;
    cacheLastCleared = GetAdjustedTime();
    cachedModifiers.clear();
}

void MaintainSmartstakeCache()
{
    int nowTime = GetAdjustedTime();
    if (cacheLastCleared + FLUSH_POLICY < nowTime) {
        LogPrintf("%s : cleared cache records (%d hit %d miss of %d total)\n", __func__, cacheHit, cacheMiss, cachedModifiers.size());
        InitSmartstakeCache();
    }
}

bool GetSmartstakeModifier(uint256 hashBlockFrom, uint64_t& nStakeModifier, int& nStakeModifierHeight, int64_t& nStakeModifierTime)
{
    nStakeModifier = 0;

    if (!::BlockIndex().count(hashBlockFrom))
        return error("GetKernelStakeModifier() : block not indexed");

    const CBlockIndex* pindexFrom = ::BlockIndex()[hashBlockFrom];
    auto nTimeBlockFrom = pindexFrom->GetBlockTime();
    nStakeModifierHeight = pindexFrom->nHeight;
    nStakeModifierTime = pindexFrom->GetBlockTime();
    int64_t nStakeModifierSelectionInterval = GetStakeModifierSelectionInterval();
    const CBlockIndex* pindex = pindexFrom;
    CBlockIndex* pindexNext = ::ChainActive()[pindexFrom->nHeight + 1];

    MaintainSmartstakeCache();

    if (cachedModifiers.find(nTimeBlockFrom) != cachedModifiers.end()) {
        nStakeModifier = cachedModifiers[nTimeBlockFrom];
        ++cacheHit;
    } else {
        while (nStakeModifierTime < nTimeBlockFrom + nStakeModifierSelectionInterval) {
            if (!pindexNext && nStakeModifier)
                return true;
            pindex = pindexNext;
            pindexNext = ::ChainActive()[pindexNext->nHeight + 1];
            if (pindex->GeneratedStakeModifier()) {
                nStakeModifierHeight = pindex->nHeight;
                nStakeModifierTime = pindex->GetBlockTime();
                nStakeModifier = pindex->nStakeModifier;
            }
        }
        ++cacheMiss;
        cachedModifiers.insert(std::make_pair(nTimeBlockFrom, nStakeModifier));
    }

    return true;
}

