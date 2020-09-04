// Copyright (c) 2020 barrystyle
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_MWALLET_H
#define BITCOIN_WALLET_MWALLET_H

#include <sync.h>
#include <util/system.h>
#include <wallet/wallet.h>

class CMWallet;
extern CMWallet mwallet;

/**
 * CMWallet class deals with CWallet like functions that are masternode related
 */
class CMWallet
{
public:
    void ListProTxCoins(std::vector<COutPoint>& vOutpts) const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
};

#endif // BITCOIN_WALLET_MWALLET_H

