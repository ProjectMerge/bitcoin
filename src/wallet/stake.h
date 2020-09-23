// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_STAKE_H
#define BITCOIN_WALLET_STAKE_H

#include <amount.h>
#include <tinyformat.h>
#include <ui_interface.h>
#include <util/message.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/system.h>
#include <wallet/wallet.h>

class CStake;
extern CStake stake;

/**
 * CStake class deals with coin minting, to be at an arms distance from wallet.cpp..
 */
class CStake
{
public:
    unsigned int nStakeSplitThreshold = 2000;
    unsigned int nHashInterval = 22;
    int nStakeSetUpdateTime = 300;

    bool MintableCoins();
    bool SelectStakeCoins(std::set<std::pair<const CWalletTx*, unsigned int> >& setCoins, CAmount nTargetAmount) const;
    bool CreateCoinStake(unsigned int nBits, CMutableTransaction& txNew, unsigned int& nTxNewTime);
    void BestStakeSeen(uint256& hash);
    uint256 ReturnBestStakeSeen();
};

#endif // BITCOIN_WALLET_STAKE_H
