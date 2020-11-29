// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTSTAKE_CACHE_H
#define SMARTSTAKE_CACHE_H

#include <validation.h>

#include <unordered_map>

class uint256;

const int FLUSH_POLICY = 45;

extern std::unordered_map<unsigned int, uint64_t> cachedModifiers;

void InitSmartstakeCache();
bool GetSmartstakeModifier(uint256 hashBlockFrom, uint64_t& nStakeModifier, int& nStakeModifierHeight, int64_t& nStakeModifierTime);

#endif // SMARTSTAKE_CACHE_H
