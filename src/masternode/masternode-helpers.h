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

class CMasternodeSigner {
public:
    bool GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet);
    bool IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey);
    bool SignMessage(std::string strMessage, std::string& errorMessage, std::vector<unsigned char>& vchSig, CKey key);
    bool VerifyMessage(CPubKey pubkey, std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage);
};

void ThreadMasternodePool();

extern CMasternodeSigner masternodeSigner;

#endif
