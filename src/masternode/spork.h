// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SPORK_H
#define SPORK_H

#include <hash.h>
#include <net.h>
#include <util/strencodings.h>

class CSporkMessage;
class CSporkManager;

namespace Spork {

enum {
    SPORK_START = 10001,
    SPORK_5_MAX_VALUE = 10004,
    SPORK_7_MASTERNODE_SCANNING = 10006,
    SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT = 10007,
    SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT = 10008,
    SPORK_10_MASTERNODE_PAY_UPDATED_NODES = 10009,
    SPORK_13_ENABLE_SUPERBLOCKS = 10012,
    SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2 = 10014,
    SPORK_16_CLIENT_COMPAT_MODE = 10015,
    SPORK_END = 10016
};

}

extern std::map<uint256, CSporkMessage> mapSporks;
extern CSporkManager sporkManager;

//
// Spork classes
// Keep track of all of the network spork settings
//

class CSporkMessage {
private:
    std::vector<unsigned char> vchSig;

public:
    int nSporkID;
    int64_t nValue;
    int64_t nTimeSigned;

    CSporkMessage(int nSporkID, int64_t nValue, int64_t nTimeSigned)
        : nSporkID(nSporkID)
        , nValue(nValue)
        , nTimeSigned(nTimeSigned)
    {
    }

    CSporkMessage()
        : nSporkID(0)
        , nValue(0)
        , nTimeSigned(0)
    {
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nSporkID);
        READWRITE(nValue);
        READWRITE(nTimeSigned);
        READWRITE(vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << nSporkID;
        ss << nValue;
        ss << nTimeSigned;
        return ss.GetHash();
    }

    bool Sign(std::string strSignKey);
    bool CheckSignature();
    void Relay(CConnman& connman);
};

class CSporkManager {
private:
    std::vector<unsigned char> vchSig;
    std::string strMasterPrivKey;
    std::map<int, CSporkMessage> mapSporksActive;

public:
    using Executor = std::function<void(void)>;
    CSporkManager() {}

    void ProcessSpork(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman);
    void ExecuteSpork(int nSporkID, int nValue);
    bool UpdateSpork(int nSporkID, int64_t nValue, CConnman& connman);

    bool IsSporkActive(int nSporkID);
    int64_t GetSporkValue(int nSporkID);
    int GetSporkIDByName(std::string strName);
    std::string GetSporkNameByID(int id);

    bool SetPrivKey(std::string strPrivKey);
};

#endif
