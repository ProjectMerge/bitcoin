// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include <validation.h>
#include <masternode/activemasternode.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode.h>
#include <masternode/masternodeman.h>
#include <masternode/netfulfilledman.h>
#include <masternode/spork.h>
#include <net.h>
#include <netmessagemaker.h>
#include <sync.h>
#include <util/system.h>
#include <addrman.h>
// clang-format on

class CMasternodeSync;
CMasternodeSync masternodeSync;

CMasternodeSync::CMasternodeSync()
{
    Reset();
}

bool CMasternodeSync::IsSynced()
{
    return RequestedMasternodeAssets == MASTERNODE_SYNC_FINISHED;
}

bool CMasternodeSync::IsMasternodeListSynced()
{
    return RequestedMasternodeAssets > MASTERNODE_SYNC_LIST;
}

bool CMasternodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (GetTime() - lastProcess > 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if (fBlockchainSynced)
        return true;

    if (fImporting || fReindex)
        return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
        return false;

    CBlockIndex* pindex = ::ChainActive().Tip();
    if (pindex == NULL)
        return false;

    if (pindex->nTime + 60 * 60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CMasternodeSync::Reset()
{
    fBlockchainSynced = false;
    lastProcess = 0;
    lastMasternodeList = 0;
    lastMasternodeWinner = 0;
    lastBudgetItem = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    mapSeenSyncBudget.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumMasternodeList = 0;
    sumMasternodeWinner = 0;
    sumBudgetItemProp = 0;
    sumBudgetItemFin = 0;
    countMasternodeList = 0;
    countMasternodeWinner = 0;
    countBudgetItemProp = 0;
    countBudgetItemFin = 0;
    RequestedMasternodeAssets = MASTERNODE_SYNC_INITIAL;
    RequestedMasternodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CMasternodeSync::AddedMasternodeList(uint256 hash)
{
    if (mnodeman.mapSeenMasternodeBroadcast.count(hash)) {
        if (mapSeenSyncMNB[hash] < MASTERNODE_SYNC_THRESHOLD) {
            lastMasternodeList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastMasternodeList = GetTime();
        mapSeenSyncMNB.insert(std::make_pair(hash, 1));
    }
}

void CMasternodeSync::AddedMasternodeWinner(uint256 hash)
{
    if (masternodePayments.mapMasternodePayeeVotes.count(hash)) {
        if (mapSeenSyncMNW[hash] < MASTERNODE_SYNC_THRESHOLD) {
            lastMasternodeWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastMasternodeWinner = GetTime();
        mapSeenSyncMNW.insert(std::make_pair(hash, 1));
    }
}

void CMasternodeSync::GetNextAsset(CConnman& connman)
{
    switch (RequestedMasternodeAssets) {
    case (MASTERNODE_SYNC_INITIAL):
    case (MASTERNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        ClearFulfilledRequest(connman);
        RequestedMasternodeAssets = MASTERNODE_SYNC_SPORKS;
        break;
    case (MASTERNODE_SYNC_SPORKS):
        RequestedMasternodeAssets = MASTERNODE_SYNC_LIST;
        break;
    case (MASTERNODE_SYNC_LIST):
        RequestedMasternodeAssets = MASTERNODE_SYNC_MNW;
        break;
    case (MASTERNODE_SYNC_MNW):
        LogPrintf("CMasternodeSync::GetNextAsset - Sync has finished\n");
        RequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
        break;
    }
    RequestedMasternodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CMasternodeSync::GetSyncStatus()
{
    switch (masternodeSync.RequestedMasternodeAssets) {
    case MASTERNODE_SYNC_INITIAL:
        return ("MNs synchronization pending...");
    case MASTERNODE_SYNC_SPORKS:
        return ("Synchronizing sporks...");
    case MASTERNODE_SYNC_LIST:
        return ("Synchronizing masternodes...");
    case MASTERNODE_SYNC_MNW:
        return ("Synchronizing masternode winners...");
    case MASTERNODE_SYNC_FAILED:
        return ("Synchronization failed");
    case MASTERNODE_SYNC_FINISHED:
        return ("Synchronization finished");
    }
    return "";
}

void CMasternodeSync::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc" || strCommand == NetMsgType::SYNCSTATUSCOUNT) {

        if (IsSynced())
            return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        switch (nItemID) {
        case (MASTERNODE_SYNC_LIST):
            if (nItemID != RequestedMasternodeAssets)
                return;
            sumMasternodeList += nCount;
            countMasternodeList++;
            break;
        case (MASTERNODE_SYNC_MNW):
            if (nItemID != RequestedMasternodeAssets)
                return;
            sumMasternodeWinner += nCount;
            countMasternodeWinner++;
            break;
        }

        LogPrintf("CMasternodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CMasternodeSync::ClearFulfilledRequest(CConnman& connman)
{
    connman.ForEachNode([](CNode* pnode) {
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "getspork");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "mnsync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "mnwsync");
    });
}

void CMasternodeSync::Process(CConnman& connman)
{
    static int tick = 0;

    if (tick++ % MASTERNODE_SYNC_TIMEOUT != 0)
        return;

    if (IsSynced()) {
        if (mnodeman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if (RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime())
        Reset();
    else if (RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED)
        return;

    LogPrintf("CMasternodeSync::Process() - tick %d RequestedMasternodeAssets %d\n", tick, RequestedMasternodeAssets);

    // Calculate "progress" for LOG reporting / GUI notification
    double nSyncProgress = double(RequestedMasternodeAttempt + (RequestedMasternodeAssets - 1) * 8) / (8*4);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);

    if (RequestedMasternodeAssets == MASTERNODE_SYNC_INITIAL)
        GetNextAsset(connman);

    //! should be a lock here, to investigate..
    std::vector<CNode*> vNodesCopy = connman.CopyNodeVector();
    for (CNode* pnode : vNodesCopy) {
        //set to synced
        if (RequestedMasternodeAssets == MASTERNODE_SYNC_SPORKS) {
            if (netfulfilledman.HasFulfilledRequest(pnode->addr, "getspork"))
                continue;
            netfulfilledman.AddFulfilledRequest(pnode->addr, "getsporks");
            connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::GETSPORKS));
            if (RequestedMasternodeAttempt >= 2)
                GetNextAsset(connman);
            RequestedMasternodeAttempt++;
            return;
        }

        if (pnode->nVersion >= masternodePayments.GetMinMasternodePaymentsProto()) {
            if (RequestedMasternodeAssets == MASTERNODE_SYNC_LIST) {
                LogPrintf("CMasternodeSync::Process() - lastMasternodeList %lld (GetTime() - MASTERNODE_SYNC_TIMEOUT) %lld\n", lastMasternodeList, GetTime() - MASTERNODE_SYNC_TIMEOUT);
                if (lastMasternodeList > 0 && lastMasternodeList < GetTime() - MASTERNODE_SYNC_TIMEOUT * 2 && RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD) {
                    GetNextAsset(connman);
                    return;
                }
                if (netfulfilledman.HasFulfilledRequest(pnode->addr, "mnsync"))
                    continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "mnsync");

                // timeout
                if (lastMasternodeList == 0 && (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
                    if (sporkManager.IsSporkActive(Spork::SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CMasternodeSync::Process - ERROR - Sync has failed on %s, will retry later\n", "MASTERNODE_SYNC_LIST");
                        RequestedMasternodeAssets = MASTERNODE_SYNC_FAILED;
                        RequestedMasternodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset(connman);
                    }
                    return;
                }

                if (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3)
                    return;

                mnodeman.DsegUpdate(pnode, connman);
                RequestedMasternodeAttempt++;
                return;
            }

            if (RequestedMasternodeAssets == MASTERNODE_SYNC_MNW) {
                if (lastMasternodeWinner > 0 && lastMasternodeWinner < GetTime() - MASTERNODE_SYNC_TIMEOUT * 2 && RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD) {
                    GetNextAsset(connman);
                    return;
                }

                if (netfulfilledman.HasFulfilledRequest(pnode->addr, "mnwsync"))
                    continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "mnwsync");

                // timeout
                if (lastMasternodeWinner == 0 && (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
                    if (sporkManager.IsSporkActive(Spork::SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CMasternodeSync::Process - ERROR - Sync has failed on %s, will retry later\n", "MASTERNODE_SYNC_MNW");
                        RequestedMasternodeAssets = MASTERNODE_SYNC_FAILED;
                        RequestedMasternodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset(connman);
                    }
                    return;
                }

                if (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3)
                    return;

                int nMnCount = mnodeman.CountEnabled();
                connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::GETMNWINNERS, nMnCount));
                RequestedMasternodeAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= PROTOCOL_VERSION) {
            if (RequestedMasternodeAssets == MASTERNODE_SYNC_BUDGET) {
                // We'll start rejecting votes if we accidentally get set as synced too soon
                if (lastBudgetItem > 0 && lastBudgetItem < GetTime() - MASTERNODE_SYNC_TIMEOUT * 2 && RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD) {
                    // Hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset(connman);

                    // Try to activate our masternode if possible
                    activeMasternode.ManageStatus(connman);

                    return;
                }

                // timeout
                if ((RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
                    // maybe there is no budgets at all, so just finish syncing
                    GetNextAsset(connman);
                    activeMasternode.ManageStatus(connman);
                    return;
                }

                if (netfulfilledman.HasFulfilledRequest(pnode->addr, "busync"))
                    continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "busync");

                if (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3)
                    return;

                uint256 n;
                connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::BUDGETVOTESYNC, n));
                RequestedMasternodeAttempt++;

                return;
            }
        }
    }
}
