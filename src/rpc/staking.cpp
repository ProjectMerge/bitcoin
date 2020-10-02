// Copyright (c) 2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/params.h>
#include <miner.h>
#include <node/context.h>
#include <pos/kernel.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <util/check.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <wallet/coincontrol.h>
#include <wallet/stake.h>
#include <wallet/wallet.h>

#include <masternode/masternode-sync.h>

#include <stdint.h>
#include <tuple>
#ifdef HAVE_MALLOC_INFO
#include <malloc.h>
#endif

#include <univalue.h>

UniValue getstakingset(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getstakingset\n"
            "Returns an object containing the staking set as if it were currently running.\n"
            "\nResult:\n"
            "{\n"
            "  \"OP_DUP OP_HASH160 4ddc8a343d0700f4836d8a2dcb2d53acdeebfc81 OP_EQUALVERIFY OP_CHECKSIG\": 1000000000,\n"
            "  \"OP_DUP OP_HASH160 8ed35742cae958032ae95dc9d5d6ccc4456ab743 OP_EQUALVERIFY OP_CHECKSIG\": 100074874543,\n"
            "  ..\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getstakingset", "") + HelpExampleRpc("getstakingset", ""));

    CCoinControl coin_control;
    auto m_wallet = GetMainWallet();
    CAmount nBalance = m_wallet->GetBalance(0, coin_control.m_avoid_address_reuse).m_mine_trusted;
    static std::set<std::pair<const CWalletTx*, unsigned int>> setStakeCoins;
    setStakeCoins.clear();

    //! build the stakeset
    UniValue obj(UniValue::VOBJ);
    if (!stake.SelectStakeCoins(setStakeCoins, nBalance))
        return obj;
    for (const auto& pcoin : setStakeCoins) {
        obj.pushKV(pcoin.first->tx->vout[pcoin.second].scriptPubKey.ToString(), pcoin.first->tx->vout[pcoin.second].nValue);
    }

    return obj;
}

UniValue getbestproofhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getbestproofhash\n"
            "Returns the staking kernel's best seen proofhash.\n"
            "\nResult:\n"
            "{\n"
            "  \"proofhash\": \"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\"\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getbestproofhash", "") + HelpExampleRpc("getbestproofhash", ""));

    UniValue obj(UniValue::VOBJ);
    uint256 bestProofHash = stake.ReturnBestStakeSeen();
    obj.pushKV("proofhash", bestProofHash.ToString());

    return obj;
}

UniValue getstakingstatus(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getstakingstatus\n"
            "Returns an object containing various staking information.\n"
            "\nResult:\n"
            "{\n"
            "  \"validtime\": true|false,          (boolean) if the chain tip is within staking phases\n"
            "  \"haveconnections\": true|false,    (boolean) if network connections are present\n"
            "  \"walletunlocked\": true|false,     (boolean) if the wallet is unlocked\n"
            "  \"mintablecoins\": true|false,      (boolean) if the wallet has mintable coins\n"
            "  \"enoughcoins\": true|false,        (boolean) if available coins are greater than reserve balance\n"
            "  \"mnsync\": true|false,             (boolean) if masternode data is synced\n"
            "  \"staking status\": true|false,     (boolean) if the wallet is staking or not\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getstakingstatus", "") + HelpExampleRpc("getstakingstatus", ""));

    CCoinControl coin_control;
    auto m_wallet = GetMainWallet();

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("validtime", ::ChainActive().Height() >= Params().GetConsensus().nLastPoWBlock);
    obj.pushKV("haveconnections", g_rpc_node->connman->GetNodeCount(CConnman::CONNECTIONS_ALL) > 0);
    if (m_wallet) {
        obj.pushKV("walletunlocked", !m_wallet->IsLocked());
        obj.pushKV("mintablecoins", stake.MintableCoins());
        obj.pushKV("enoughcoins", m_wallet->GetBalance(0, coin_control.m_avoid_address_reuse).m_mine_trusted > 0);
    }
    obj.pushKV("mnsync", masternodeSync.IsSynced());

    bool nStaking = false;

    if (m_wallet->m_last_coin_stake_search_interval > 0)
        nStaking = true;

    obj.pushKV("staking status", nStaking);

    return obj;
}

void RegisterStakingRPCCommands(CRPCTable &t)
{
// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "staking",            "getstakingset",          &getstakingset,          {} },
    { "staking",            "getbestproofhash",       &getbestproofhash,       {} },
    { "staking",            "getstakingstatus",       &getstakingstatus,       {} },
};
// clang-format on

    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
