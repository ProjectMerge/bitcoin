// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <legacyclients.h>

#include <masternode/spork.h>
#include <net.h>
#include <net_processing.h>
#include <wallet/wallet.h>

//! formerly referred to as 'training wheels' mode

bool IsLegacyNode(const CNode* pfrom)
{
     if (pfrom->nRecvVersion < newClientProtocol)
         return true;
     return false;
}

bool IsHeadersNode(const CNode* pfrom)
{
     if (pfrom->cleanSubVer == "Merge:0.20.0")
         return true;
     return false;
}

bool IsLegacyMode()
{
     if (sporkManager.IsSporkActive(Spork::SPORK_16_CLIENT_COMPAT_MODE))
         return false;
     return true;
}

