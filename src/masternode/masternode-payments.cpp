// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternodedb.h>
#include <masternode/masternodeman.h>
#include <masternode/netfulfilledman.h>
#include <masternode/spork.h>
#include <net_processing.h>
#include <netmessagemaker.h>
#include <util/moneystr.h>

/** Object for who's going to get paid on which blocks */
CMasternodePayments masternodePayments;

RecursiveMutex cs_vecPayments;
RecursiveMutex cs_mapMasternodeBlocks;
RecursiveMutex cs_mapMasternodePayeeVotes;

extern int ActiveProtocol();

uint256 CMasternodePaymentWinner::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << payee;
    ss << nBlockHeight;
    ss << vinMasternode.prevout;
    return ss.GetHash();
}

std::string CMasternodePaymentWinner::GetStrMessage() const
{
    return vinMasternode.prevout.ToStringShort() + std::to_string(nBlockHeight) + payee.ToString();
}

bool CMasternodePaymentWinner::IsValid(CNode* pnode, std::string& strError, CConnman& connman)
{
    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (!pmn) {
        strError = strprintf("Unknown Masternode %s", vinMasternode.prevout.hash.ToString());
        LogPrint(BCLog::MASTERNODE, "CMasternodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinMasternode, connman);
        return false;
    }

    if (pmn->protocolVersion < PROTOCOL_VERSION) {
        strError = strprintf("Masternode protocol too old %d - req %d", pmn->protocolVersion, PROTOCOL_VERSION);
        LogPrint(BCLog::MASTERNODE, "CMasternodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetMasternodeRank(vinMasternode, nBlockHeight - 100, PROTOCOL_VERSION);

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Masternode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint(BCLog::MASTERNODE, "CMasternodePaymentWinner::IsValid - %s\n", strError);
            //if (masternodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

void CMasternodePaymentWinner::Relay(CConnman& connman)
{
    CInv inv(MSG_MASTERNODE_WINNER, GetHash());
    connman.RelayInv(inv);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted)
{
    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    if (pindexPrev == nullptr)
        return true;

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = ::BlockIndex().find(block.hashPrevBlock);
        if (mi != ::BlockIndex().end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint(BCLog::MASTERNODE, "IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    }

    return true;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
    if (!masternodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrint(BCLog::MASTERNODE, "Client not synced, skipping block payee checks\n");
        return true;
    }

    const CTransactionRef& txNew = block.vtx[1];

    //check for masternode payee
    if (masternodePayments.IsTransactionValid(txNew, nBlockHeight))
        return true;
    LogPrint(BCLog::MASTERNODE, "Invalid mn payment detected %s\n", txNew->ToString().c_str());

    if (sporkManager.IsSporkActive(Spork::SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrint(BCLog::MASTERNODE, "Masternode payment enforcement is disabled, accepting block\n");

    return true;
}

void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake, bool fZPIVStake)
{
    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    if (!pindexPrev)
        return;

    masternodePayments.FillBlockPayee(txNew, nFees, fProofOfStake, fZPIVStake);
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    return masternodePayments.GetRequiredPaymentsString(nBlockHeight);
}

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake, bool fZPIVStake)
{
    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    if (!pindexPrev)
        return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if (!masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, payee)) {
        //no masternode detected
        CMasternode* winningNode = mnodeman.GetCurrentMasterNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(PKHash(winningNode->pubKeyCollateralAddress));
        } else {
            LogPrint(BCLog::MASTERNODE, "CreateNewBlock: Failed to detect masternode to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockSubsidy(pindexPrev->nHeight, Params().GetConsensus());
    CAmount masternodePayment = GetMasternodePayment(::ChainActive().Height(), blockValue);

    if (hasPayment) {
        if (fProofOfStake) {
            /**For Proof Of Stake vout[0] must be null
             * Stake reward can be split into many different outputs, so we must
             * use vout.size() to align with several different cases.
             * An additional output is appended as the masternode payment
             */
            unsigned int i = txNew.vout.size();
            txNew.vout.resize(i + 1);
            txNew.vout[i].scriptPubKey = payee;
            txNew.vout[i].nValue = masternodePayment;

            //subtract mn payment from the stake reward
            {
                if (i == 2) {
                    // Majority of cases; do it quick and move on
                    txNew.vout[i - 1].nValue -= masternodePayment;
                } else if (i > 2) {
                    // special case, stake is split between (i-1) outputs
                    unsigned int outputs = i - 1;
                    CAmount mnPaymentSplit = masternodePayment / outputs;
                    CAmount mnPaymentRemainder = masternodePayment - (mnPaymentSplit * outputs);
                    for (unsigned int j = 1; j <= outputs; j++) {
                        txNew.vout[j].nValue -= mnPaymentSplit;
                    }
                    // in case it's not an even division, take the last bit of dust from the last one
                    txNew.vout[outputs].nValue -= mnPaymentRemainder;
                }
            }

        } else {
            txNew.vout.resize(2);
            txNew.vout[1].scriptPubKey = payee;
            txNew.vout[1].nValue = masternodePayment;
            txNew.vout[0].nValue = blockValue - masternodePayment;
        }

        CTxDestination address1;
        ExtractDestination(payee, address1);

        LogPrint(BCLog::MASTERNODE, "Masternode payment of %s to %s\n", FormatMoney(masternodePayment).c_str(), EncodeDestination(address1));
    }
}

int CMasternodePayments::GetMinMasternodePaymentsProto()
{
    return ActiveProtocol();
}

void CMasternodePayments::ProcessMessageMasternodePayments(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if (!masternodeSync.IsBlockchainSynced())
        return;

    if (strCommand == "mnget") {
        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (netfulfilledman.HasFulfilledRequest(pfrom->addr, "mnget")) {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessMessageMasternodePayments() : mnget - peer already asked me for the list\n");
            return;
        }

        netfulfilledman.AddFulfilledRequest(pfrom->addr, "mnget");
        masternodePayments.Sync(pfrom, nCountNeeded, connman);
        LogPrint(BCLog::MASTERNODE, "mnget - Sent Masternode winners to peer %i\n", pfrom->GetId());

    } else if (strCommand == "mnw") {

        //this is required in litemodef
        CMasternodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < PROTOCOL_VERSION)
            return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || ::ChainActive().Tip() == nullptr)
                return;
            nHeight = ::ChainActive().Tip()->nHeight;
        }

        if (masternodePayments.mapMasternodePayeeVotes.count(winner.GetHash())) {
            LogPrint(BCLog::MASTERNODE, "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint(BCLog::MASTERNODE, "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError, connman)) {
            if (strError != "")
                LogPrint(BCLog::MASTERNODE, "mnw - invalid message - %s\n", strError);
            return;
        }

        if (!masternodePayments.CanVote(winner.vinMasternode.prevout, winner.nBlockHeight)) {
            LogPrint(BCLog::MASTERNODE, "mnw - masternode already voted - %s\n", winner.vinMasternode.prevout.ToStringShort());
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        LogPrint(BCLog::MASTERNODE, "mnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", EncodeDestination(address1), winner.nBlockHeight, nHeight, winner.vinMasternode.prevout.ToStringShort());

        if (masternodePayments.AddWinningMasternode(winner)) {
            winner.Relay(connman);
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
        }
    }
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CMasternodePayments::IsScheduled(CMasternode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || ::ChainActive().Tip() == nullptr)
            return false;
        nHeight = ::ChainActive().Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(PKHash(mn.pubKeyCollateralAddress));

    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight)
            continue;
        if (mapMasternodeBlocks.count(h)) {
            if (mapMasternodeBlocks[h].GetPayee(payee)) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CMasternodePayments::AddWinningMasternode(CMasternodePaymentWinner& winnerIn)
{
    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapMasternodePayeeVotes, cs_mapMasternodeBlocks);

        if (mapMasternodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapMasternodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapMasternodeBlocks.count(winnerIn.nBlockHeight)) {
            CMasternodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapMasternodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    mapMasternodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CMasternodeBlockPayees::IsTransactionValid(const CTransactionRef& txNew)
{
    LOCK(cs_vecPayments);

    //require at least 6 signatures
    int nMaxSignatures = 0;
    for (CMasternodePayee& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED)
        return true;

    std::string strPayeesPossible = "";
    CAmount requiredMasternodePayment = GetMasternodePayment(::ChainActive().Height(), GetBlockSubsidy(::ChainActive().Height(), Params().GetConsensus()));

    for (CMasternodePayee& payee : vecPayments) {
        bool found = false;
        for (CTxOut out : txNew->vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if (out.nValue == requiredMasternodePayment)
                    found = true;
                else
                    LogPrint(BCLog::MASTERNODE, "%s : Masternode payment value (%s) different from required value (%s).\n",
                        __func__, FormatMoney(out.nValue).c_str(), FormatMoney(requiredMasternodePayment).c_str());
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found)
                return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);

            if (strPayeesPossible == "") {
                strPayeesPossible += EncodeDestination(address1);
            } else {
                strPayeesPossible += "," + EncodeDestination(address1);
            }
        }
    }

    LogPrint(BCLog::MASTERNODE, "CMasternodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredMasternodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (CMasternodePayee& payee : vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);

        if (ret != "Unknown") {
            ret += ", " + EncodeDestination(address1) + ":" + std::to_string(payee.nVotes);
        } else {
            ret = EncodeDestination(address1) + ":" + std::to_string(payee.nVotes);
        }
    }

    return ret;
}

std::string CMasternodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CMasternodePayments::IsTransactionValid(const CTransactionRef& txNew, int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CMasternodePayments::CleanPaymentList()
{
    LOCK2(cs_mapMasternodePayeeVotes, cs_mapMasternodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || ::ChainActive().Tip() == nullptr)
            return;
        nHeight = ::ChainActive().Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() * 1.25), 1000);

    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::CleanPaymentList - Removing old Masternode payment - block %d\n", winner.nBlockHeight);
            masternodeSync.mapSeenSyncMNW.erase((*it).first);
            mapMasternodePayeeVotes.erase(it++);
            mapMasternodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CMasternodePayments::ProcessBlock(int nBlockHeight, CConnman& connman)
{
    if (!fMasternode)
        return false;

    //reference node - hybrid mode

    int n = mnodeman.GetMasternodeRank(activeMasternode.vin, nBlockHeight - 100, PROTOCOL_VERSION);

    if (n == -1) {
        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock - Unknown Masternode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock - Masternode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight)
        return false;

    CMasternodePaymentWinner newWinner(activeMasternode.vin);

    {
        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeMasternode.vin.prevout.hash.ToString());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CMasternode* pmn = mnodeman.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount);

        if (pmn) {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(PKHash(pmn->pubKeyCollateralAddress));
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);

            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", EncodeDestination(address1).c_str(), newWinner.nBlockHeight);
        } else {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() Failed to find masternode to pay\n");
        }
    }

    std::string errorMessage;
    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if (!masternodeSigner.GetKeysFromSecret(strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() - Error upon calling GetKeysFromSecret.\n");
        return false;
    }

    LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() - AddWinningMasternode\n");

    if (AddWinningMasternode(newWinner)) {
        newWinner.Relay(connman);
        nLastBlockHeight = nBlockHeight;
        return true;
    }

    return false;
}

void CMasternodePayments::Sync(CNode* node, int nCountNeeded, CConnman& connman)
{
    LOCK(cs_mapMasternodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || ::ChainActive().Tip() == nullptr)
            return;
        nHeight = ::ChainActive().Tip()->nHeight;
    }

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount)
        nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_MASTERNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    connman.PushMessage(node, CNetMsgMaker(node->GetSendVersion()).Make(NetMsgType::SYNCSTATUSCOUNT, MASTERNODE_SYNC_MNW, nInvCount));
}

std::string CMasternodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}

int CMasternodePayments::GetOldestBlock()
{
    LOCK(cs_mapMasternodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CMasternodeBlockPayees>::iterator it = mapMasternodeBlocks.begin();
    while (it != mapMasternodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}

int CMasternodePayments::GetNewestBlock()
{
    LOCK(cs_mapMasternodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CMasternodeBlockPayees>::iterator it = mapMasternodeBlocks.begin();
    while (it != mapMasternodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
