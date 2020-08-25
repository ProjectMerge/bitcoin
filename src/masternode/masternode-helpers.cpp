// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <masternode/activemasternode.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternodeman.h>
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

bool CMasternodeSigner::VerifyMessage(CPubKey pubkey, std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage, const char* caller)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey2;
    if (!pubkey2.RecoverCompact(ss.GetHash(), vchSig)) {
        errorMessage = "Error recovering public key.";
        return false;
    }

    auto verifyResult = PKHash(pubkey2) == PKHash(pubkey);
    if (!verifyResult)
        LogPrint(BCLog::MASTERNODE, "CMasternodeSigner::VerifyMessage -- keys don't match: %s %s (called by %s)\n", pubkey2.GetID().ToString(), pubkey.GetID().ToString(), caller);
    else
        LogPrint(BCLog::MASTERNODE, "CMasternodeSigner::VerifyMessage -- keys match: %s %s (called by %s)\n", pubkey2.GetID().ToString(), pubkey.GetID().ToString(), caller);

    return verifyResult;
}

void ThreadMasternodePool()
{
    if (ShutdownRequested())
        return;
    if (::ChainstateActive().IsInitialBlockDownload())
        return;

    static unsigned int c = 0;

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

bool GetMasternodeVinAndKeys(CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex)
{
    std::vector<COutput> vPossibleCoins = activeMasternode.SelectCoinsMasternode();
    if (vPossibleCoins.empty()) {
        LogPrintf("CWallet::GetMasternodeVinAndKeys -- Could not locate any valid masternode vin\n");
        return false;
    }

    if (strTxHash.empty())
        return GetVinAndKeysFromOutput(vPossibleCoins[0], txinRet, pubKeyRet, keyRet);

    // Find specific vin
    uint256 txHash = uint256S(strTxHash);
    int nOutputIndex = atoi(strOutputIndex.c_str());

    for (COutput& out : vPossibleCoins)
        if (out.tx->GetHash() == txHash && out.i == nOutputIndex)
            return GetVinAndKeysFromOutput(out, txinRet, pubKeyRet, keyRet);

    return false;
}

bool GetVinAndKeysFromOutput(COutput out, CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet)
{
    CScript pubScript;
    txinRet = CTxIn(out.tx->GetHash(), out.i);
    pubScript = out.tx->tx->vout[out.i].scriptPubKey;

    auto m_wallet = GetMainWallet();
    LegacyScriptPubKeyMan* spk_man = m_wallet->GetLegacyScriptPubKeyMan();

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CKeyID keyID = GetKeyForDestination(*spk_man, address1);
    if (keyID.IsNull()) {
        LogPrintf("CWallet::GetVinAndKeysFromOutput -- Address does not refer to a key\n");
        return false;
    }

    if (!spk_man->GetKey(keyID, keyRet)) {
        LogPrintf("CWallet::GetVinAndKeysFromOutput -- Private key for address is not known\n");
        return false;
    }

    pubKeyRet = keyRet.GetPubKey();
    return true;
}
