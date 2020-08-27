// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <masternode/activemasternode.h>
#include <masternode/masternodeconfig.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternodeman.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <shutdown.h>
#include <util/validation.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>

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

void CMasternodeSigner::SetKey(std::string strSecret, CKey& key, CPubKey& pubkey)
{
    auto m_wallet = GetMainWallet();
    EnsureLegacyScriptPubKeyMan(*m_wallet, true);

    key = DecodeSecret(strSecret);
    pubkey = key.GetPubKey();
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

// retrieve all collateral-like outputs from current wallet
std::vector<COutput> SelectCoinsMasternode()
{
    std::vector<COutput> vCoins;
    std::vector<COutput> filteredCoins;
    std::vector<COutPoint> confLockedCoins;

    auto m_wallet = GetMainWallet();

    // Temporary unlock MN coins from masternode.conf
    if (mnConfigTotal > -1) {
        uint256 mnTxHash;
        for (const auto mne : masternodeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());
            COutPoint outpoint = COutPoint(mnTxHash, atoi(mne.getOutputIndex().c_str()));
            confLockedCoins.push_back(outpoint);
            m_wallet->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    AvailableCollaterals(vCoins);

    // Lock MN coins from masternode.conf back if they were temporary unlocked
    if (!confLockedCoins.empty()) {
        for (COutPoint outpoint : confLockedCoins)
            m_wallet->LockCoin(outpoint);
    }

    // Filter
    for (const COutput& out : vCoins) {
        if (out.tx->tx->vout[out.i].nValue == Params().GetConsensus().nCollateralAmount) {
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// cutdown version of AvailableCoins specifically for masternode collaterals
void AvailableCollaterals(std::vector<COutput>& vCoins)
{
    auto m_wallet = GetMainWallet();
    auto locked_chain = m_wallet->chain().lock();

    vCoins.clear();
    CCoinControl coinControl;
    std::set<uint256> trusted_parents;

    for (const auto& entry : m_wallet->mapWallet)
    {
        const uint256& wtxid = entry.first;
        const CWalletTx& wtx = entry.second;

        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < Params().GetConsensus().nMasternodeMinimumConfirmations) {
            continue;
        }

        for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {

            isminetype mine = m_wallet->IsMine(wtx.tx->vout[i]);
            if (mine == ISMINE_NO || m_wallet->IsSpentKey(wtxid, i)) {
                continue;
            }

            if (wtx.tx->vout[i].nValue != Params().GetConsensus().nCollateralAmount) {
                continue;
            }

            std::unique_ptr<SigningProvider> provider = m_wallet->GetSolvingProvider(wtx.tx->vout[i].scriptPubKey);
            bool solvable = provider ? IsSolvable(*provider, wtx.tx->vout[i].scriptPubKey) : false;
            bool spendable = ((mine & ISMINE_SPENDABLE) != ISMINE_NO) || (((mine & ISMINE_WATCH_ONLY) != ISMINE_NO));
            vCoins.push_back(COutput(&wtx, i, nDepth, spendable, solvable, true));
        }
    }
}

bool GetMasternodeVinAndKeys(CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex)
{
    std::vector<COutput> vPossibleCoins = SelectCoinsMasternode();
    if (vPossibleCoins.empty()) {
        LogPrintf("CWallet::GetMasternodeVinAndKeys -- Could not locate any valid masternode vin\n");
        return false;
    }

    if (strTxHash.empty())
        return GetVinFromOutput(vPossibleCoins[0], txinRet, pubKeyRet, keyRet);

    // Find specific vin
    uint256 txHash = uint256S(strTxHash);
    int nOutputIndex = atoi(strOutputIndex.c_str());

    for (COutput& out : vPossibleCoins)
        if (out.tx->GetHash() == txHash && out.i == nOutputIndex)
            return GetVinFromOutput(out, txinRet, pubKeyRet, keyRet);

    return false;
}

bool GetVinFromOutput(COutput out, CTxIn& vinRet, CPubKey& pubkeyRet, CKey& secretKey)
{
    CScript pubScript;
    auto m_wallet = GetMainWallet();
    vinRet = CTxIn(out.tx->tx->GetHash(), out.i);
    pubScript = out.tx->tx->vout[out.i].scriptPubKey;

    CTxDestination address;
    ExtractDestination(pubScript, address);
    const PKHash *pkhash = boost::get<PKHash>(&address);
    if (!pkhash) {
        LogPrintf("GetVinFromOutput -- Address does not refer to a key\n");
        return false;
    }

    LegacyScriptPubKeyMan* spk_man = m_wallet->GetLegacyScriptPubKeyMan();
    if (!spk_man) {
        LogPrintf ("GetVinFromOutput -- This type of wallet does not support this command\n");
        return false;
    }

    CKeyID keyID(*pkhash);
    if (!spk_man->GetKey(keyID, secretKey)) {
        LogPrintf ("GetVinFromOutput -- Private key for address is not known\n");
        return false;
    }

    pubkeyRet = secretKey.GetPubKey();
    return true;
}

