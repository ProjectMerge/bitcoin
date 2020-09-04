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
#include <masternode/netfulfilledman.h>
#include <masternode/spork.h>

#include <evo/deterministicmns.h>
#include <evo/mnauth.h>
#include <evo/simplifiedmns.h>
#include <llmq/quorums_blockprocessor.h>
#include <llmq/quorums_commitment.h>
#include <llmq/quorums_chainlocks.h>
#include <llmq/quorums_dkgsessionmgr.h>
#include <llmq/quorums_init.h>
#include <llmq/quorums_signing.h>
#include <llmq/quorums_signing_shares.h>

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
            {
                CSporkMessage spork;
                return sporkManager.GetSporkByHash(inv.hash, spork);
            }
        case MSG_QUORUM_FINAL_COMMITMENT:
            return llmq::quorumBlockProcessor->HasMinableCommitment(inv.hash);
        case MSG_QUORUM_CONTRIB:
        case MSG_QUORUM_COMPLAINT:
        case MSG_QUORUM_JUSTIFICATION:
        case MSG_QUORUM_PREMATURE_COMMITMENT:
            return llmq::quorumDKGSessionManager->AlreadyHave(inv);
        case MSG_QUORUM_RECOVERED_SIG:
            return llmq::quorumSigningManager->AlreadyHave(inv);
        case MSG_CLSIG:
            return llmq::chainLocksHandler->AlreadyHave(inv);
    }
    // Don't know what it is, just say we already got one
    return true;
}

void ProcessGetDataMasternodeTypes(CNode* pfrom, const CChainParams& chainparams, CConnman* connman, const CTxMemPool& mempool, const CInv& inv, bool& push) LOCKS_EXCLUDED(cs_main)
{
    const CNetMsgMaker msgMaker(PROTOCOL_VERSION);

    if (!push && inv.type == MSG_SPORK) {
        CSporkMessage spork;
        if(sporkManager.GetSporkByHash(inv.hash, spork)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::SPORK, spork));
            push = true;
        }
    }

    if (!push && (inv.type == MSG_QUORUM_FINAL_COMMITMENT)) {
        llmq::CFinalCommitment o;
        if (llmq::quorumBlockProcessor->GetMinableCommitmentByHash(inv.hash, o)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::QFCOMMITMENT, o));
            push = true;
        }
    }

    if (!push && (inv.type == MSG_QUORUM_CONTRIB)) {
        llmq::CDKGContribution o;
        if (llmq::quorumDKGSessionManager->GetContribution(inv.hash, o)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::QCONTRIB, o));
            push = true;
        }
    }
    if (!push && (inv.type == MSG_QUORUM_COMPLAINT)) {
        llmq::CDKGComplaint o;
        if (llmq::quorumDKGSessionManager->GetComplaint(inv.hash, o)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::QCOMPLAINT, o));
            push = true;
        }
    }
    if (!push && (inv.type == MSG_QUORUM_JUSTIFICATION)) {
        llmq::CDKGJustification o;
        if (llmq::quorumDKGSessionManager->GetJustification(inv.hash, o)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::QJUSTIFICATION, o));
            push = true;
        }
    }
    if (!push && (inv.type == MSG_QUORUM_PREMATURE_COMMITMENT)) {
        llmq::CDKGPrematureCommitment o;
        if (llmq::quorumDKGSessionManager->GetPrematureCommitment(inv.hash, o)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::QPCOMMITMENT, o));
            push = true;
        }
    }
    if (!push && (inv.type == MSG_QUORUM_RECOVERED_SIG)) {
        llmq::CRecoveredSig o;
        if (llmq::quorumSigningManager->GetRecoveredSigForGetData(inv.hash, o)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::QSIGREC, o));
            push = true;
        }
    }

    if (!push && (inv.type == MSG_CLSIG)) {
        llmq::CChainLockSig o;
        if (llmq::chainLocksHandler->GetChainLockByHash(inv.hash, o)) {
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::CLSIG, o));
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
        sporkManager.ProcessSpork(pfrom, msg_type, vRecv, *connman);
        masternodeSync.ProcessMessage(pfrom, msg_type, vRecv);
        CMNAuth::ProcessMessage(pfrom, msg_type, vRecv, *connman);
        llmq::quorumBlockProcessor->ProcessMessage(pfrom, msg_type, vRecv, *connman);
        llmq::quorumDKGSessionManager->ProcessMessage(pfrom, msg_type, vRecv, *connman);
        llmq::quorumSigSharesManager->ProcessMessage(pfrom, msg_type, vRecv, *connman);
        llmq::quorumSigningManager->ProcessMessage(pfrom, msg_type, vRecv, *connman);
        llmq::chainLocksHandler->ProcessMessage(pfrom, msg_type, vRecv, *connman);
    }

    return true;
}
