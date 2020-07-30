// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

    const CBlockIndex* BlockLastSolved = pindexLast;
    const CBlockIndex* BlockReading = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 24;
    int64_t PastBlocksMax = 24;
    int64_t CountBlocks = 0;
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;
    unsigned int nPowTargetLimit = UintToArith256(params.powLimit).GetCompact();

    if (BlockLastSolved == nullptr || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < PastBlocksMin) {
        return nPowTargetLimit;
    }

    if (pindexLast->nHeight > params.nLastPoWBlock)
    {
        arith_uint256 bnPosTargetLimit = UintToArith256(params.posLimit);
        int64_t nPosTargetSpacing = params.nPosTargetSpacing;
        int64_t nPosTargetTimespan = params.nPosTargetTimespan;

        int64_t nActualSpacing = 0;
        if (pindexLast->nHeight != 0)
            nActualSpacing = pindexLast->GetBlockTime() - pindexLast->pprev->GetBlockTime();
        if (nActualSpacing < 0)
            nActualSpacing = 1;

        // ppcoin: target change every block
        // ppcoin: retarget with exponential moving toward target spacing
        arith_uint256 bnNew;
        bnNew.SetCompact(pindexLast->nBits);
        int64_t nInterval = nPosTargetTimespan / nPosTargetSpacing;
        bnNew *= ((nInterval - 1) * nPosTargetSpacing + nActualSpacing + nActualSpacing);
        bnNew /= ((nInterval + 1) * nPosTargetSpacing);

        if (bnNew <= 0 || bnNew > bnPosTargetLimit)
            bnNew = bnPosTargetLimit;

        return bnNew.GetCompact();
    }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) {
            break;
        }
        CountBlocks++;

        if (CountBlocks <= PastBlocksMin) {
            if (CountBlocks == 1)
                PastDifficultyAverage.SetCompact(BlockReading->nBits);
            else
                PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks) + (UintToArith256(uint256()).SetCompact(BlockReading->nBits))) / (CountBlocks + 1);
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        if (LastBlockTime > 0) {
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += Diff;
        }
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == nullptr) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    arith_uint256 bnNew(PastDifficultyAverage);

    // Limit adjustment step
    int64_t nPowTargetTimespan = CountBlocks * params.nPowTargetSpacing;
    if (nActualTimespan < nPowTargetTimespan / 3)
        nActualTimespan = nPowTargetTimespan / 3;
    if (nActualTimespan > nPowTargetTimespan * 3)
        nActualTimespan = nPowTargetTimespan * 3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nPowTargetTimespan;

    if (bnNew > UintToArith256(params.powLimit))
        bnNew = UintToArith256(params.powLimit);

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
