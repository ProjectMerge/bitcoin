// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternode/activemasternode.h>

#include <key_io.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternodeconfig.h>
#include <masternode/masternodeman.h>
#include <net.h>
#include <netbase.h>
#include <util/system.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>

CActiveMasternode activeMasternode;

//
// Bootup the Masternode, look for a 10000 MERGE input and register on the network
//
void CActiveMasternode::ManageStatus(CConnman& connman)
{
    std::string errorMessage = "";
    auto m_wallet = GetMainWallet();

    if (!fMasternode)
        return;

    if (!startMessage) {
        startMessage = true;
        LogPrintf("CActiveMasternode::ManageStatus() - Begin\n");
    }

    if (!masternodeSync.IsBlockchainSynced()) {
        status = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveMasternode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_MASTERNODE_SYNC_IN_PROCESS)
        status = ACTIVE_MASTERNODE_INITIAL;

    if (status == ACTIVE_MASTERNODE_INITIAL) {
        CMasternode* pmn;
        pmn = mnodeman.Find(pubKeyMasternode);
        if (pmn) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION)
                EnableHotColdMasterNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_MASTERNODE_STARTED) {
        // Set defaults
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "";

        if (m_wallet->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        CCoinControl coin_control;
        if (m_wallet->GetBalance(0, coin_control.m_avoid_address_reuse).m_mine_trusted == 0) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strMasterNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the masternodeaddr configuration option.";
                LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strMasterNodeAddr);
        }

        LogPrintf("CActiveMasternode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        SOCKET hSocket = INVALID_SOCKET;
        hSocket = CreateSocket(service);
        if (hSocket == INVALID_SOCKET) {
            LogPrintf("CActiveMasternode::ManageStateInitial -- Could not create socket '%s'\n", service.ToString());
            return;
        }
        bool fConnected = ConnectSocketDirectly(service, hSocket, nConnectTimeout, true) && IsSelectableSocket(hSocket);
        CloseSocket(hSocket);

        if (!fConnected) {
            status = ACTIVE_MASTERNODE_NOT_CAPABLE;
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveMasternode::ManageStateInitial -- %s\n", notCapableReason);
            return;
        }

        // Choose coins to use
        CKey keyCollateralAddress;
        CPubKey pubKeyCollateralAddress;
        if (GetMasternodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress))
        {
            auto masternodeConfirms = GetUTXOConfirmations(vin.prevout);
            if (masternodeConfirms < MASTERNODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_MASTERNODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), masternodeConfirms);
                LogPrintf("CActiveMasternode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(m_wallet->cs_wallet);
            m_wallet->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyMasternode;
            CKey keyMasternode;

            masternodeSigner.SetKey(strMasterNodePrivKey, keyMasternode, pubKeyMasternode);

            CMasternodeBroadcast mnb;
            if (!CreateBroadcast(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyMasternode, pubKeyMasternode, errorMessage, mnb)) {
                notCapableReason = "Error on Register: " + errorMessage;
                LogPrintf("CActiveMasternode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //send to all peers
            LogPrintf("CActiveMasternode::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            mnb.Relay(connman);

            LogPrintf("CActiveMasternode::ManageStatus() - Is capable masternode!\n");
            status = ACTIVE_MASTERNODE_STARTED;
            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveMasternode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendMasternodePing(errorMessage, connman)) {
        LogPrintf("CActiveMasternode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveMasternode::GetStatus()
{
    switch (status) {
    case ACTIVE_MASTERNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_MASTERNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Masternode";
    case ACTIVE_MASTERNODE_INPUT_TOO_NEW:
        return strprintf("Masternode input must have at least %d confirmations", MASTERNODE_MIN_CONFIRMATIONS);
    case ACTIVE_MASTERNODE_NOT_CAPABLE:
        return "Not capable masternode: " + notCapableReason;
    case ACTIVE_MASTERNODE_STARTED:
        return "Masternode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveMasternode::SendMasternodePing(std::string& errorMessage, CConnman& connman)
{
    if (status != ACTIVE_MASTERNODE_STARTED) {
        errorMessage = "Masternode is not in a running status";
        return false;
    }

    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if (!masternodeSigner.GetKeysFromSecret(strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
        errorMessage = "Error upon calling GetKeysFromSecret.\n";
        return false;
    }

    LogPrintf("CActiveMasternode::SendMasternodePing() - Relay Masternode Ping vin = %s\n", vin.ToString());

    CMasternodePing mnp(vin);
    if (!mnp.Sign(keyMasternode, pubKeyMasternode)) {
        errorMessage = "Couldn't sign Masternode Ping";
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    CMasternode* pmn = mnodeman.Find(vin);
    if (pmn) {
        if (pmn->IsPingedWithin(MASTERNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Masternode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        mnodeman.mapSeenMasternodePing.insert(std::make_pair(mnp.GetHash(), mnp));

        //mnodeman.mapSeenMasternodeBroadcast.lastPing is probably outdated, so we'll update it
        CMasternodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenMasternodeBroadcast.count(hash))
            mnodeman.mapSeenMasternodeBroadcast[hash].lastPing = mnp;

        mnp.Relay(connman);
        return true;

    } else {
        // Seems like we are trying to send a ping while the Masternode is not registered in the network
        errorMessage = "Masternode List doesn't include our Masternode, shutting down Masternode pinging service! " + vin.ToString();
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

bool CActiveMasternode::CreateBroadcast(std::string strService, std::string strKeyMasternode, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CMasternodeBroadcast& mnb, bool fOffline)
{
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    //need correct blocks to send ping
    if (!masternodeSync.IsBlockchainSynced()) {
        errorMessage = "Sync in progress. Must wait until sync is complete to start Masternode";
        LogPrintf("CActiveMasternode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!masternodeSigner.GetKeysFromSecret(strKeyMasternode, keyMasternode, pubKeyMasternode)) {
        errorMessage = strprintf("Can't find keys for masternode %s", strService);
        LogPrintf("CActiveMasternode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!GetMasternodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for masternode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveMasternode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    return CreateBroadcast(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyMasternode, pubKeyMasternode, errorMessage, mnb);
}

bool CActiveMasternode::CreateBroadcast(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyMasternode, CPubKey pubKeyMasternode, std::string& errorMessage, CMasternodeBroadcast& mnb)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex)
        return false;

    CMasternodePing mnp(vin);
    if (!mnp.Sign(keyMasternode, pubKeyMasternode)) {
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveMasternode::CreateBroadcast() -  %s\n", errorMessage);
        mnb = CMasternodeBroadcast();
        return false;
    }

    mnb = CMasternodeBroadcast(service, vin, pubKeyCollateralAddress, pubKeyMasternode, PROTOCOL_VERSION);
    mnb.lastPing = mnp;
    if (!mnb.Sign(keyCollateralAddress)) {
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveMasternode::CreateBroadcast() - %s\n", errorMessage);
        mnb = CMasternodeBroadcast();
        return false;
    }

    return true;
}

bool CActiveMasternode::GetMasternodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    return GetMasternodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveMasternode::GetMasternodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex)
{
    std::vector<COutput> possibleCoins = SelectCoinsMasternode();
    COutput* selectedOutput;

    // Find the vin
    if (!strTxHash.empty()) {
        uint256 txHash(uint256S(strTxHash));
        int outputIndex = atoi(strOutputIndex.c_str());
        bool found = false;
        for (COutput& out : possibleCoins) {
            if (out.tx->GetHash() == txHash && out.i == outputIndex) {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if (!found) {
            LogPrintf("CActiveMasternode::GetMasternodeVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if (possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveMasternode::GetMasternodeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}

// when starting a Masternode, this can enable to run as a hot wallet with no funds
bool CActiveMasternode::EnableHotColdMasterNode(CTxIn& newVin, CService& newService)
{
    if (!fMasternode)
        return false;

    status = ACTIVE_MASTERNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveMasternode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
