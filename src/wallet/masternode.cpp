// Copyright (c) 2020 barrystyle
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/masternode.h>

#include <util/system.h>
#include <wallet/wallet.h>

#include <evo/providertx.h>

#include <llmq/quorums_chainlocks.h>

CMWallet mwallet;

void CMWallet::ListProTxCoins(std::vector<COutPoint>& vOutpts) const
{
    auto wallet = GetMainWallet();
    auto mnList = deterministicMNManager->GetListAtChainTip();

    AssertLockHeld(wallet->cs_wallet);
    for (const auto& pair : wallet->mapWallet) {
        for (unsigned int i = 0; i < pair.second.tx->vout.size(); ++i) {
            if (wallet->IsMine(pair.second.tx->vout[i]) && !wallet->IsSpent(pair.first, i)) {
                if (deterministicMNManager->IsProTxWithCollateral(pair.second.tx, i) || mnList.HasMNByCollateral(COutPoint(pair.first, i))) {
                    vOutpts.emplace_back(COutPoint(pair.first, i));
                }
            }
        }
    }
}

