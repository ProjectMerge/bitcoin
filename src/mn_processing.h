// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MN_PROCESSING_H
#define BITCOIN_MN_PROCESSING_H

#include <consensus/params.h>
#include <consensus/validation.h>
#include <net.h>
#include <net_processing.h>
#include <sync.h>
#include <validationinterface.h>

class CChainParams;
class CTxMemPool;

/** Return the current protocol version in use */
int ActiveProtocol();

bool AlreadyHaveMasternodeTypes(const CInv& inv, const CTxMemPool& mempool);
void ProcessGetDataMasternodeTypes(CNode* pfrom, const CChainParams& chainparams, CConnman* connman, const CTxMemPool& mempool, const CInv& inv, bool& push);
bool ProcessMessageMasternodeTypes(CNode* pfrom, const std::string& msg_type, CDataStream& vRecv, int64_t nTimeReceived, const CChainParams& chainparams, CTxMemPool& mempool, CConnman* connman, BanMan* banman, const std::atomic<bool>& interruptMsgProc);

#endif // BITCOIN_MN_PROCESSING_H
