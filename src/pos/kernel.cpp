// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2018 PIVX Developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>

#include <chainparams.h>
#include <db.h>
#include <init.h>
#include <policy/policy.h>
#include <pos/kernel.h>
#include <script/interpreter.h>
#include <timedata.h>
#include <txdb.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <numeric>

#define PRI64x "llx"
using namespace std;

unsigned int getModifierInterval()
{
    return MODIFIER_INTERVAL;
}

static bool GetLastStakeModifier(const CBlockIndex* pindex, uint64_t& nStakeModifier, int64_t& nModifierTime)
{
    if (!pindex)
        return error("GetLastStakeModifier: null pindex");

    while (pindex && pindex->pprev && !pindex->GeneratedStakeModifier())
        pindex = pindex->pprev;

    if (!pindex->GeneratedStakeModifier())
        return error("GetLastStakeModifier: no generation at genesis block");

    nStakeModifier = pindex->nStakeModifier;
    nModifierTime = pindex->GetBlockTime();
    return true;
}

static int64_t GetStakeModifierSelectionIntervalSection(int nSection)
{
    assert(nSection >= 0 && nSection < 64);
    return getModifierInterval() * 63 / (63 + ((63 - nSection) * (MODIFIER_INTERVAL_RATIO - 1)));
}

static int64_t GetStakeModifierSelectionInterval()
{
    int64_t nSelectionInterval = 0;
    for (int nSection = 0; nSection < 64; nSection++) {
        nSelectionInterval += GetStakeModifierSelectionIntervalSection(nSection);
    }
    return nSelectionInterval;
}

static bool SelectBlockFromCandidates(vector<pair<int64_t, uint256>>& vSortedByTimestamp, map<uint256, const CBlockIndex*>& mapSelectedBlocks, int64_t nSelectionIntervalStop, uint64_t nStakeModifierPrev, const CBlockIndex** pindexSelected)
{
    bool fModifierV2 = false;
    bool fFirstRun = true;
    bool fSelected = false;
    arith_uint256 hashBest = 0;
    *pindexSelected = nullptr;

    for (const auto& item : vSortedByTimestamp) {
        const CBlockIndex* pindex = LookupBlockIndex(item.second);

        if (!pindex)
            return error("SelectBlockFromCandidates: failed to find block index for candidate block %s", item.second.ToString());

        if (fSelected && pindex->GetBlockTime() > nSelectionIntervalStop)
            break;

        if (fFirstRun) {
            fModifierV2 = pindex->nHeight >= (int)Params().GetConsensus().ModifierUpgradeBlock();
            fFirstRun = false;
        }

        if (mapSelectedBlocks.count(pindex->GetBlockHash()) > 0)
            continue;

        uint256 hashProof = fModifierV2 ? pindex->GetBlockHash() : (pindex->IsProofOfStake() ? uint256() : pindex->GetBlockHash());

        CDataStream ss(SER_GETHASH, 0);
        ss << hashProof << nStakeModifierPrev;
        arith_uint256 hashSelection = UintToArith256(Hash(ss.begin(), ss.end()));

        // the selection hash is divided by 2**32 so that proof-of-stake block
        // is always favored over proof-of-work block. this is to preserve
        // the energy efficiency property
        if (pindex->IsProofOfStake())
            hashSelection >>= 32;

        if (fSelected && hashSelection < hashBest) {
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*)pindex;
        } else if (!fSelected) {
            fSelected = true;
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*)pindex;
        }
    }

    if (gArgs.GetBoolArg("-printstakemodifier", false))
        LogPrintf("SelectBlockFromCandidates: selection hash=%s\n", hashBest.ToString().c_str());

    return fSelected;
}

bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier)
{
    nStakeModifier = 0;
    fGeneratedStakeModifier = false;

    if (!pindexPrev) {
        fGeneratedStakeModifier = true;
        return true;
    }

    if (pindexPrev->nHeight == 0) {
        fGeneratedStakeModifier = true;
        nStakeModifier = uint64_t("stakemodifier");
        return true;
    }

    int64_t nModifierTime = 0;

    if (!GetLastStakeModifier(pindexPrev, nStakeModifier, nModifierTime))
        return error("ComputeNextStakeModifier: unable to get last modifier");

    if (gArgs.GetBoolArg("-printstakemodifier", false))
        LogPrintf("ComputeNextStakeModifier: prev modifier= %s time=%d\n", boost::lexical_cast<std::string>(nStakeModifier).c_str(), nModifierTime);

    if (nModifierTime / getModifierInterval() >= pindexPrev->GetBlockTime() / getModifierInterval())
        return true;

    vector<pair<int64_t, uint256>> vSortedByTimestamp;
    vSortedByTimestamp.reserve(64 * getModifierInterval() / Params().GetConsensus().nPowTargetSpacing);
    int64_t nSelectionInterval = GetStakeModifierSelectionInterval();
    int64_t nSelectionIntervalStart = (pindexPrev->GetBlockTime() / getModifierInterval()) * getModifierInterval() - nSelectionInterval;
    const CBlockIndex* pindex = pindexPrev;

    while (pindex && pindex->GetBlockTime() >= nSelectionIntervalStart) {
        vSortedByTimestamp.push_back(make_pair(pindex->GetBlockTime(), pindex->GetBlockHash()));
        pindex = pindex->pprev;
    }

    int nHeightFirstCandidate = pindex ? (pindex->nHeight + 1) : 0;

    // Shuffle before sort
    for (int i = vSortedByTimestamp.size() - 1; i > 1; --i)
        std::swap(vSortedByTimestamp[i], vSortedByTimestamp[GetRand(i)]);

    sort(vSortedByTimestamp.begin(), vSortedByTimestamp.end(), [](const std::pair<int64_t, uint256>& a, const std::pair<int64_t, uint256>& b) {
        if (a.first != b.first)
            return a.first < b.first;
        // Timestamp equals - compare block hashes
        const uint32_t* pa = a.second.GetDataPtr();
        const uint32_t* pb = b.second.GetDataPtr();
        int cnt = 256 / 32;
        do {
            --cnt;
            if (pa[cnt] != pb[cnt])
                return pa[cnt] < pb[cnt];
        } while (cnt);
        return false; // Elements are equal
    });

    uint64_t nStakeModifierNew = 0;
    int64_t nSelectionIntervalStop = nSelectionIntervalStart;
    map<uint256, const CBlockIndex*> mapSelectedBlocks;

    for (int nRound = 0; nRound < min(64, (int)vSortedByTimestamp.size()); nRound++) {
        nSelectionIntervalStop += GetStakeModifierSelectionIntervalSection(nRound);

        if (!SelectBlockFromCandidates(vSortedByTimestamp, mapSelectedBlocks, nSelectionIntervalStop, nStakeModifier, &pindex))
            return error("ComputeNextStakeModifier: unable to select block at round %d", nRound);

        nStakeModifierNew |= (((uint64_t)pindex->GetStakeEntropyBit()) << nRound);
        mapSelectedBlocks.insert(make_pair(pindex->GetBlockHash(), pindex));

        if (gArgs.GetBoolArg("-printstakemodifier", false))
            LogPrintf("ComputeNextStakeModifier: selected round %d stop=%d height=%d bit=%d\n",
                nRound, nSelectionIntervalStop, pindex->nHeight, pindex->GetStakeEntropyBit());
    }

    if (gArgs.GetBoolArg("-printstakemodifier", false)) {
        string strSelectionMap = "";
        strSelectionMap.insert(0, pindexPrev->nHeight - nHeightFirstCandidate + 1, '-');
        pindex = pindexPrev;

        while (pindex && pindex->nHeight >= nHeightFirstCandidate) {
            if (pindex->IsProofOfStake())
                strSelectionMap.replace(pindex->nHeight - nHeightFirstCandidate, 1, "=");

            pindex = pindex->pprev;
        }

        for (const auto& item : mapSelectedBlocks) {
            strSelectionMap.replace(item.second->nHeight - nHeightFirstCandidate, 1, item.second->IsProofOfStake() ? "S" : "W");
        }

        LogPrintf("ComputeNextStakeModifier: selection height [%d, %d] map %s\n", nHeightFirstCandidate, pindexPrev->nHeight, strSelectionMap.c_str());
    }

    if (gArgs.GetBoolArg("-printstakemodifier", false)) {
        LogPrintf("ComputeNextStakeModifier: new modifier=%s time=%d\n", boost::lexical_cast<std::string>(nStakeModifierNew).c_str(), pindexPrev->GetBlockTime());
    }

    nStakeModifier = nStakeModifierNew;
    fGeneratedStakeModifier = true;
    return true;
}

bool GetKernelStakeModifier(uint256 hashBlockFrom, uint64_t& nStakeModifier, int& nStakeModifierHeight, int64_t& nStakeModifierTime, bool fPrintProofOfStake)
{
    nStakeModifier = 0;

    if (!::BlockIndex().count(hashBlockFrom))
        return error("GetKernelStakeModifier() : block not indexed");

    const CBlockIndex* pindexFrom = ::BlockIndex()[hashBlockFrom];
    nStakeModifierHeight = pindexFrom->nHeight;
    nStakeModifierTime = pindexFrom->GetBlockTime();
    int64_t nStakeModifierSelectionInterval = GetStakeModifierSelectionInterval();
    const CBlockIndex* pindex = pindexFrom;
    CBlockIndex* pindexNext = ::ChainActive()[pindexFrom->nHeight + 1];

    // loop to find the stake modifier later by a selection interval
    while (nStakeModifierTime < pindexFrom->GetBlockTime() + nStakeModifierSelectionInterval) {
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

    nStakeModifier = pindex->nStakeModifier;
    return true;
}

uint256 stakeHash(unsigned int nTimeTx, CDataStream ss, unsigned int prevoutIndex, uint256 prevoutHash, unsigned int nTimeBlockFrom)
{
    ss << nTimeBlockFrom << prevoutIndex << prevoutHash << nTimeTx;
    uint256 proofHash = Hash(ss.begin(), ss.end());
    return proofHash;
}

void DebugStakeHash(uint64_t currentModifier, unsigned int nTimeBlockFrom, unsigned int prevoutn, uint256 prevouthash, unsigned int nTimeTx)
{
    LogPrintf("modifier %016llx ntimeblockfrom %d prevoutn %d prevouthash %s ntimetx %d\n", currentModifier, nTimeBlockFrom, prevoutn, prevouthash.ToString().c_str(), nTimeTx);
}

bool stakeTargetHit(uint256 hashProofOfStake, int64_t nValueIn, uint256 bnTargetPerCoinDay)
{
    arith_uint256 bnCoinDayWeight = arith_uint256(nValueIn) / 100;
    arith_uint256 bnTarget = bnCoinDayWeight * UintToArith256(bnTargetPerCoinDay);
    return UintToArith256(hashProofOfStake) < bnTarget;
}

bool CheckStakeKernelHash(unsigned int nBits, const CBlock blockFrom, const CTransactionRef txPrev, const COutPoint prevout, unsigned int& nTimeTx, unsigned int nHashDrift, bool fCheck, uint256& hashProofOfStake, bool fPrintProofOfStake)
{
    int64_t nValueIn = txPrev->vout[prevout.n].nValue;
    unsigned int nTimeBlockFrom = blockFrom.GetBlockTime();

    if (nTimeTx < nTimeBlockFrom)
        return error("CheckStakeKernelHash() : nTime violation");

    if (nTimeBlockFrom + Params().GetConsensus().nMinStakeAge > nTimeTx)
        return error("CheckStakeKernelHash() : min age violation - nTimeBlockFrom=%d nStakeMinAge=%d nTimeTx=%d", nTimeBlockFrom, 3600, nTimeTx);

    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);
    uint64_t nStakeModifier = 0;
    int nStakeModifierHeight = 0;
    int64_t nStakeModifierTime = 0;

    if (!GetKernelStakeModifier(blockFrom.GetHash(), nStakeModifier, nStakeModifierHeight, nStakeModifierTime, fPrintProofOfStake)) {
        LogPrintf("CheckStakeKernelHash(): failed to get kernel stake modifier \n");
        return false;
    }

    if (gArgs.GetBoolArg("-printstakemodifier", false)) {
        DebugStakeHash(nStakeModifier, blockFrom.nTime, prevout.n, prevout.hash, nTimeTx);
    }

    CDataStream ss(SER_GETHASH, 0);
    ss << nStakeModifier;

    if (fCheck) {
        hashProofOfStake = stakeHash(nTimeTx, ss, prevout.n, prevout.hash, nTimeBlockFrom);
        return stakeTargetHit(hashProofOfStake, nValueIn, ArithToUint256(bnTargetPerCoinDay));
    }

    bool fSuccess = false;
    unsigned int nTryTime = 0;
    unsigned int i;
    int nHeightStart = ::ChainActive().Height();

    for (i = 0; i < (nHashDrift); i++) {
        if (::ChainActive().Height() != nHeightStart)
            break;

        nTryTime = nTimeTx + nHashDrift - i;
        hashProofOfStake = stakeHash(nTryTime, ss, prevout.n, prevout.hash, nTimeBlockFrom);

        if (!stakeTargetHit(hashProofOfStake, nValueIn, ArithToUint256(bnTargetPerCoinDay)))
            continue;

        fSuccess = true;
        nTimeTx = nTryTime;

        if (fPrintProofOfStake) {
            LogPrintf("CheckStakeKernelHash() : using modifier %s at height=%d timestamp=%s for block from height=%d timestamp=%d\n",
                boost::lexical_cast<std::string>(nStakeModifier).c_str(), nStakeModifierHeight, nStakeModifierTime,
                ::BlockIndex()[blockFrom.GetHash()]->nHeight,
                blockFrom.GetBlockTime());
            LogPrintf("CheckStakeKernelHash() : pass protocol=%s modifier=%s nTimeBlockFrom=%u prevoutHash=%s nTimeTxPrev=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
                "0.3",
                boost::lexical_cast<std::string>(nStakeModifier).c_str(),
                nTimeBlockFrom, prevout.hash.ToString().c_str(), nTimeBlockFrom, prevout.n, nTryTime,
                hashProofOfStake.ToString().c_str());
        }

        break;
    }

    mapHashedBlocks.clear();
    mapHashedBlocks[::ChainActive().Tip()->nHeight] = GetTime();
    return fSuccess;
}

bool CheckProofOfStake(const CBlock block, uint256& hashProofOfStake)
{
    const CTransactionRef tx = block.vtx[1];

    if (!tx->IsCoinStake())
        return error("CheckProofOfStake() : called on non-coinstake %s", tx->GetHash().ToString());

    // Kernel (input 0) must match the stake hash target per coin age (nBits)
    const CTxIn& txin = tx->vin[0];

    // Get transaction index for the previous transaction
    CDiskTxPos postx;

    if (!pblocktree->ReadTxIndex(txin.prevout.hash, postx))
        return error("CheckProofOfStake() : tx index not found"); // tx index not found

    // Read txPrev and header of its block
    CBlockHeader header;
    CTransactionRef txPrev;
    {
        CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);

        try {
            file >> header;
            fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
            file >> txPrev;
        } catch (std::exception& e) {
            return error("%s() : deserialize or I/O error in CheckProofOfStake()", __PRETTY_FUNCTION__);
        }

        if (txPrev->GetHash() != txin.prevout.hash)
            return error("%s() : txid mismatch in CheckProofOfStake()", __PRETTY_FUNCTION__);
    }

    unsigned int nInterval = 0;
    unsigned int nTime = block.nTime;

    if (!CheckStakeKernelHash(block.nBits, header, txPrev, txin.prevout, nTime, nInterval, true, hashProofOfStake))
        return error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s \n", tx->GetHash().ToString().c_str(), hashProofOfStake.ToString().c_str());

    return true;
}
