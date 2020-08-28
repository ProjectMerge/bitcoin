// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Feirm developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEHELPERS_H
#define MASTERNODEHELPERS_H

#include <base58.h>
#include <pubkey.h>
#include <sync.h>
#include <validation.h>

class COutput;

class CMasternodeSigner {
public:
    bool GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet);
    void SetKey(std::string strSecret, CKey& key, CPubKey& pubkey);
    bool IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey);
    bool SignMessage(std::string strMessage, std::string& errorMessage, std::vector<unsigned char>& vchSig, CKey key);
#if __APPLE__
    bool VerifyMessage(CPubKey pubkey, std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage, const char* caller = "n/a");
#else
    bool VerifyMessage(CPubKey pubkey, std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage, const char* caller = __builtin_FUNCTION());
#endif
};

std::vector<COutput> SelectCoinsMasternode();
void AvailableCollaterals(std::vector<COutput>& vCoins);
bool GetMasternodeVinAndKeys(CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex);
bool GetVinFromOutput(COutput out, CTxIn& vinRet, CPubKey& pubkeyRet, CKey& secretKey);
void ThreadMasternodePool();

extern CMasternodeSigner masternodeSigner;

#endif
