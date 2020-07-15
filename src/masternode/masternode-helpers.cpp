// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <masternode/activemasternode.h>
#include <masternode/masternodeman.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternode-payments.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <shutdown.h>
#include <util/validation.h>

#include <boost/thread/thread.hpp>

CMasternodeSigner masternodeSigner;

bool CMasternodeSigner::GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet)
{
    keyRet = DecodeSecret(strSecret);
    if (!keyRet.IsValid())
        return false;

    pubkeyRet = keyRet.GetPubKey();

    return true;
}

bool CMasternodeSigner::IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey)
{
    CScript payee2 = GetScriptForDestination(PKHash(pubkey));

    CTransactionRef txVin;
    uint256 hash;
    if (GetTransaction(vin.prevout.hash, txVin, Params().GetConsensus(), hash)) {
        for (CTxOut out : txVin->vout) {
            if (out.nValue == Params().GetConsensus().nCollateralAmount) {
                if (out.scriptPubKey == payee2)
                    return true;
            }
        }
    }

    return false;
}

bool CMasternodeSigner::SignMessage(std::string strMessage, std::string& errorMessage, std::vector<unsigned char>& vchSig, CKey key)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        errorMessage = "Signing failed.";
        return false;
    }

    return true;
}

bool CMasternodeSigner::VerifyMessage(CPubKey pubkey, std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey2;
    if (!pubkey2.RecoverCompact(ss.GetHash(), vchSig)) {
        errorMessage = "Error recovering public key.";
        return false;
    }

    if (pubkey2.GetID() != pubkey.GetID())
        LogPrint(BCLog::MASTERNODE, "CMasternodeSigner::VerifyMessage -- keys don't match: %s %s\n", pubkey2.GetID().ToString(), pubkey.GetID().ToString());

    return (pubkey2.GetID() == pubkey.GetID());
}

void ThreadMasternodePool()
{
    unsigned int c = 0;

    //! spin until chain synced
    while (!masternodeSync.IsBlockchainSynced() && !ShutdownRequested()) {
        MilliSleep(1000);
    }

    if (ShutdownRequested())
        return;

    //! chain is done
    while (true) {

        boost::this_thread::interruption_point();

        MilliSleep(1000);

        // try to sync from all available nodes, one step at a time
        masternodeSync.Process(*g_rpc_node->connman);

        if (masternodeSync.IsBlockchainSynced()) {
            c++;

            // check if we should activate or ping every few minutes,
            // start right after sync is considered to be done
            if (c % MASTERNODE_PING_SECONDS == 1)
                activeMasternode.ManageStatus(*g_rpc_node->connman);

            if (c % 60 == 0) {
                mnodeman.CheckAndRemove();
                mnodeman.ProcessMasternodeConnections(*g_rpc_node->connman);
                masternodePayments.CleanPaymentList();
            }
        }
    }
}

