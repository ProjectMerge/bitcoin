// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <net.h>
#include <net_processing.h>

class CNode;

const int newClientProtocol = 80000;

bool IsLegacyNode(const CNode* pfrom);
bool IsLegacyMode();
