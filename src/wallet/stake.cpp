// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2015-2019 Tom Bradshaw (presstab)
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/stake.h>

#include <key_io.h>
#include <masternode/masternode-payments.h>
#include <policy/policy.h>
#include <pos/kernel.h>
#include <wallet/coincontrol.h>

CStake stake;

bool CStake::SelectStakeCoins(std::set<std::pair<const CWalletTx*, unsigned int>>& setCoins, CAmount nTargetAmount) const
{
    auto m_wallet = GetMainWallet();
    auto locked_chain = m_wallet->chain().lock();
    LOCK(m_wallet->cs_wallet);

    std::vector<COutput> vCoins;
    m_wallet->AvailableCoins(*locked_chain, vCoins, true);
    CAmount nAmountSelected = 0;

    for (const COutput& out : vCoins) {
        //make sure not to outrun target amount
        if (nAmountSelected + out.tx->tx->vout[out.i].nValue > nTargetAmount)
            continue;

        //check for min age
        if (GetAdjustedTime() - out.tx->GetTxTime() < Params().GetConsensus().MinStakeAge())
            continue;

        //check that it is matured
        if (out.nDepth < (out.tx->tx->IsCoinStake() ? COINBASE_MATURITY : 10))
            continue;

        //check that it isnt a collateral type amount
        if (out.tx->tx->vout[out.i].nValue == Params().GetConsensus().nCollateralAmount)
            continue;

        //add to our stake set
        setCoins.insert(std::make_pair(out.tx, out.i));
        nAmountSelected += out.tx->tx->vout[out.i].nValue;
    }
    return true;
}

bool CStake::MintableCoins()
{
    auto m_wallet = GetMainWallet();
    auto locked_chain = m_wallet->chain().lock();
    LOCK(m_wallet->cs_wallet);

    CCoinControl coin_control;
    CAmount nBalance = m_wallet->GetBalance(0, coin_control.m_avoid_address_reuse).m_mine_trusted;
    if (nBalance <= 0)
        return false;

    std::vector<COutput> vCoins;
    m_wallet->AvailableCoins(*locked_chain, vCoins, true);

    for (const COutput& out : vCoins) {
        if (GetAdjustedTime() - out.tx->GetTxTime() > Params().GetConsensus().MinStakeAge())
            return true;
    }

    return false;
}

uint256 bestHash = uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
void CStake::BestStakeSeen(uint256& hash)
{
    if (UintToArith256(hash) < UintToArith256(bestHash)) {
        if (hash != uint256()) {
            bestHash = hash;
            LogPrintf("best proofHash seen: %s\n", bestHash.ToString().c_str());
        }
    }
}

typedef std::vector<unsigned char> valtype;
bool CStake::CreateCoinStake(unsigned int nBits, CMutableTransaction& txNew, unsigned int& nTxNewTime)
{
    txNew.vin.clear();
    txNew.vout.clear();

    // Mark coin stake transaction
    CScript scriptEmpty;
    scriptEmpty.clear();
    txNew.vout.push_back(CTxOut(0, scriptEmpty));

    // Choose coins to use
    auto m_wallet = GetMainWallet();
    if (!m_wallet)
        return false;

    CCoinControl coin_control;
    CAmount nBalance = m_wallet->GetBalance(0, coin_control.m_avoid_address_reuse).m_mine_trusted;

    if (nBalance <= 0)
        return false;

    static std::set<std::pair<const CWalletTx*, unsigned int>> setStakeCoins;
    static int nLastStakeSetUpdate = 0;

    if (GetTime() - nLastStakeSetUpdate > nStakeSetUpdateTime) {
        setStakeCoins.clear();
        if (!SelectStakeCoins(setStakeCoins, nBalance))
            return false;

        nLastStakeSetUpdate = GetTime();
    }

    if (setStakeCoins.empty())
        return false;

    //! required as cwallet isnt acceptable now..
    LegacyScriptPubKeyMan* spk_man = m_wallet->GetLegacyScriptPubKeyMan();
    if (!spk_man) {
        LogPrint(BCLog::POS, "CreateCoinStake : failed to get signing provider\n");
        return false;
    }

    CAmount nCredit = 0;
    CScript scriptPubKeyKernel;
    std::unique_ptr<SigningProvider> provider;
    std::vector<std::pair<const CWalletTx*, unsigned int>> vwtxPrev;

    for (const auto& pcoin : setStakeCoins) {
        // Read block header
        CBlockIndex* blockIndex = LookupBlockIndex(pcoin.first->m_confirm.hashBlock);
        CBlockHeader block = blockIndex->GetBlockHeader();

        bool fKernelFound = false;
        uint256 hashProofOfStake = uint256();
        COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
        nTxNewTime = GetAdjustedTime();
        unsigned int nMaxDrift = Params().GetConsensus().nMaxHashDrift;
        {
            bool hashFound = CheckStakeKernelHash(nBits, block, pcoin.first->tx, prevoutStake, nTxNewTime, nMaxDrift, false, hashProofOfStake);
            BestStakeSeen(hashProofOfStake);
            if (hashFound) {
                if (nTxNewTime <= ::ChainActive().Tip()->GetMedianTimePast()) {
                    LogPrint(BCLog::POS, "%s : kernel found, but it is too far in the past\n", __func__);
                    continue;
                }

                // Found a kernel
                if (gArgs.GetBoolArg("-printcoinstake", false))
                    LogPrintf("CreateCoinStake : kernel found\n");

                std::vector<valtype> vSolutions;
                CScript scriptPubKeyOut;
                scriptPubKeyKernel = pcoin.first->tx->vout[pcoin.second].scriptPubKey;
                txnouttype whichType = Solver(scriptPubKeyKernel, vSolutions);
                if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH) {
                    LogPrint(BCLog::POS, "%s: no support for kernel type=%d\n", __func__, whichType);
                    break;
                }

                LogPrintf("CStake::CreateCoinStake(): parsed kernel type=%d\n", whichType);

                if (whichType == TX_PUBKEYHASH) {
                    provider = m_wallet->GetSolvingProvider(scriptPubKeyKernel);
                    if (!provider) {
                        LogPrint(BCLog::POS, "%s: failed to obtain a signing/solving provider\n", __func__);
                        break;
                    }

                    CKey key;
                    if (provider->GetKey(CKeyID(uint160(vSolutions[0])), key)) {
                        LogPrint(BCLog::POS, "%s: failed to get key for kernel type=%d\n", __func__, whichType);
                        break;
                    }
                    scriptPubKeyOut << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;
                } else {
                    scriptPubKeyOut = scriptPubKeyKernel;
                }

                // continued...
                txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
                nCredit += pcoin.first->tx->vout[pcoin.second].nValue;
                vwtxPrev.push_back(pcoin);
                txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));
                const CBlockIndex* pIndex0 = ::ChainActive().Tip();
                uint64_t nTotalSize = pcoin.first->tx->vout[pcoin.second].nValue + GetBlockSubsidy(pIndex0->nHeight, Params().GetConsensus());

                // stakesplitthreshold in multiples of COIN
                if (nTotalSize / 2 > nStakeSplitThreshold * COIN)
                    txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));

                if (gArgs.GetBoolArg("-printcoinstake", false))
                    LogPrintf("CreateCoinStake : added kernel type=%d\n", whichType);

                fKernelFound = true;
                break;
            }
            if (fKernelFound)
                break;
        }
    }

    if (nCredit == 0 || nCredit > nBalance)
        return false;

    // Calculate reward
    CAmount nReward;
    const CBlockIndex* pIndex0 = ::ChainActive().Tip();
    nReward = GetBlockSubsidy(pIndex0->nHeight, Params().GetConsensus());
    nCredit += nReward;

    CAmount nMinFee = 0;
    while (true) {
        // Set output amount
        if (txNew.vout.size() == 3) {
            txNew.vout[1].nValue = ((nCredit - nMinFee) / 2 / CENT) * CENT;
            txNew.vout[2].nValue = nCredit - nMinFee - txNew.vout[1].nValue;
        } else
            txNew.vout[1].nValue = nCredit - nMinFee;

        // Limit size
        unsigned int nBytes = ::GetSerializeSize(txNew, PROTOCOL_VERSION);
        if (nBytes >= DEFAULT_BLOCK_MAX_WEIGHT / 5)
            return error("CreateCoinStake : exceeded coinstake size limit");

        // No fees
        CAmount nFeeNeeded = 0;

        // Check enough fee is paid
        if (nMinFee < nFeeNeeded) {
            nMinFee = nFeeNeeded;
            continue; // try signing again
        } else {
            break;
        }
    }

    //Masternode payment
    FillBlockPayee(txNew, 0, true, false);

    // Sign the input coins
    int nIn = 0;
    for (const auto pcoin : vwtxPrev) {
        if (!SignSignature(*spk_man, *pcoin.first->tx, txNew, nIn++, SIGHASH_ALL))
            return error("CreateCoinStake : failed to sign coinstake");
    }

    // Successfully generated coinstake
    nLastStakeSetUpdate = 0; //this will trigger stake set to repopulate next round
    return true;
}

