// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <mn_processing.h>

#include <chainparams.h>
#include <net_processing.h>
#include <netmessagemaker.h>
#include <util/system.h>
#include <util/strencodings.h>

#include <masternode/activemasternode.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternodeman.h>
#include <masternode/spork.h>

#include <memory>
#include <typeinfo>

#if defined(NDEBUG)
# error "Bitcoin cannot be compiled without assertions."
#endif

int ActiveProtocol()
{
    return PROTOCOL_VERSION;
}

bool AlreadyHaveMasternodeTypes(const CInv& inv, const CTxMemPool& mempool)
{
    switch (inv.type)
    {
        case MSG_SPORK:
            return mapSporks.count(inv.hash);
        case MSG_MASTERNODE_WINNER:
            if (masternodePayments.mapMasternodePayeeVotes.count(inv.hash)) {
                masternodeSync.AddedMasternodeWinner(inv.hash);
                return true;
            }
            return false;
        case MSG_MASTERNODE_ANNOUNCE:
            if (mnodeman.mapSeenMasternodeBroadcast.count(inv.hash)) {
                masternodeSync.AddedMasternodeList(inv.hash);
                return true;
            }
            return false;
        case MSG_MASTERNODE_PING:
            return mnodeman.mapSeenMasternodePing.count(inv.hash);
    }
    // Don't know what it is, just say we already got one
    return true;
}

void ProcessGetDataMasternodeTypes(CNode* pfrom, const CChainParams& chainparams, CConnman* connman, const CTxMemPool& mempool, const CInv& inv, bool& push) LOCKS_EXCLUDED(cs_main)
{
    const CNetMsgMaker msgMaker(PROTOCOL_VERSION);

    if (!push && inv.type == MSG_SPORK) {
        if(mapSporks.count(inv.hash)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::SPORK, mapSporks[inv.hash]));
            push = true;
        }
    }

    if (!push && inv.type == MSG_MASTERNODE_WINNER) {
        if (masternodePayments.mapMasternodePayeeVotes.count(inv.hash)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::MNWINNER, masternodePayments.mapMasternodePayeeVotes[inv.hash]));
            push = true;
        }
    }

    if (!push && inv.type == MSG_MASTERNODE_ANNOUNCE) {
        if(mnodeman.mapSeenMasternodeBroadcast.count(inv.hash)){
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::MNBROADCAST, mnodeman.mapSeenMasternodeBroadcast[inv.hash]));
            push = true;
        }
    }

    if (!push && inv.type == MSG_MASTERNODE_PING) {
        if(mnodeman.mapSeenMasternodePing.count(inv.hash)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::MNPING, mnodeman.mapSeenMasternodePing[inv.hash]));
            push = true;
        }
    }
}

bool ProcessMessageMasternodeTypes(CNode* pfrom, const std::string& msg_type, CDataStream& vRecv, int64_t nTimeReceived, const CChainParams& chainparams, CTxMemPool& mempool, CConnman* connman, BanMan* banman, const std::atomic<bool>& interruptMsgProc)
{
    bool found = false;
    const std::vector<std::string> &allMessages = getAllNetMessageTypes();
    for (const std::string msg : allMessages) {
        if (msg == msg_type) {
            found = true;
             break;
        }
    }

    if (found) {
        mnodeman.ProcessMessage(pfrom, msg_type, vRecv, *connman);
        masternodePayments.ProcessMessageMasternodePayments(pfrom, msg_type, vRecv, *connman);
        sporkManager.ProcessSpork(pfrom, msg_type, vRecv, *connman);
        masternodeSync.ProcessMessage(pfrom, msg_type, vRecv, *connman);
    }

    return true;
}
