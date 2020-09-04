// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <util/system.h>

void initVectors();
bool isExceptionBlock(int nHeight, const Consensus::Params& consensusParams);
bool isExceptionTx(uint256& hash);
