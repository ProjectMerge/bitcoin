// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/system.h>
#include <uint256.h>

std::vector<uint256> exceptionBlocks;
std::vector<uint256> exceptionTransactions;

void initVectors() {
    exceptionTransactions.push_back(uint256S("2b77d68f79c8c45b77335607c928533950da763a4a16c34555bdf8446aa6cc1c")); //! mainnet genesis tx
    exceptionTransactions.push_back(uint256S("705ea6c69f9003f9f45e9e02f8d541a98a0edd231de7e1a25b937a5b21085096")); //! testnet genesis tx
    exceptionBlocks.push_back(uint256S("f1913b55f235523257677ec69be377d3fd2cbcbe5ce630f4c8a6cf952b10cfec"));       //! segwit incident
}

bool isExceptionBlock(uint256& hash) {
    for (const auto item : exceptionBlocks)
      if (hash == item)
         return true;
    return false;
}

bool isExceptionTx(uint256& hash) {
    for (const auto item : exceptionTransactions)
      if (hash == item)
         return true;
    return false;
}
