// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2020 The MERGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKSIGNATURE_H
#define BLOCKSIGNATURE_H

#include <key.h>
#include <primitives/block.h>
#include <script/signingprovider.h>

bool SignBlock(CBlock& block, const SigningProvider& keystore);
bool CheckBlockSignature(const CBlock& block);

#endif // BLOCKSIGNATURE_H
