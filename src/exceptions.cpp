// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <util/system.h>
#include <uint256.h>

std::vector<uint256> exceptionBlocks;
std::vector<uint256> exceptionTransactions;

void initVectors() {
    exceptionBlocks.push_back(uint256S("f1913b55f235523257677ec69be377d3fd2cbcbe5ce630f4c8a6cf952b10cfec"));       //! segwit incident
}

bool isExceptionBlock(int nHeight, const Consensus::Params& consensusParams) {
    return nHeight < consensusParams.DIP0003Height;
}

bool isExceptionTx(uint256& hash) {
    for (const auto item : exceptionTransactions)
      if (hash == item)
         return true;
    return false;
}
