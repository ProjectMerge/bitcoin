// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <init.h>
#include <key_io.h>
#include <masternode/activemasternode.h>
#include <masternode/masternode-helpers.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode.h>
#include <masternode/masternodeman.h>
#include <util/validation.h>
#include <validation.h>

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
    CScript payee2 = GetScriptForDestination(pubkey.GetID());

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
        LogPrintf("CMasternodeSigner::VerifyMessage -- keys don't match: %s %s\n", pubkey2.GetID().ToString(), pubkey.GetID().ToString());

    return (pubkey2.GetID() == pubkey.GetID());
}
