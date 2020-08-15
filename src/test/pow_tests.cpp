// Copyright (c) 2015-2019 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    int nHeight = 49152;

    // build the chain of 24 blocks
    CBlockIndex blockIndexLast;
    blockIndexLast.nHeight = nHeight;
    blockIndexLast.nTime = 1548769958; // block 49152
    blockIndexLast.nBits = 0x1c269ab9;
    CBlockIndex blockIndexPrev1 = CBlockIndex();
    blockIndexPrev1.nHeight = --nHeight;
    blockIndexPrev1.nTime = 1548769957; // block 49151
    blockIndexPrev1.nBits = 0x1c24e84c;
    blockIndexLast.pprev = &blockIndexPrev1;
    CBlockIndex blockIndexPrev2 = CBlockIndex();
    blockIndexPrev2.nHeight = --nHeight;
    blockIndexPrev2.nTime = 1548769902; // block 49150
    blockIndexPrev2.nBits = 0x1c249e2f;
    blockIndexPrev1.pprev = &blockIndexPrev2;
    CBlockIndex blockIndexPrev3 = CBlockIndex();
    blockIndexPrev3.nHeight = --nHeight;
    blockIndexPrev3.nTime = 1548769895; // block 49149
    blockIndexPrev3.nBits = 0x1c24bc1b;
    blockIndexPrev2.pprev = &blockIndexPrev3;
    CBlockIndex blockIndexPrev4 = CBlockIndex();
    blockIndexPrev4.nHeight = --nHeight;
    blockIndexPrev4.nTime = 1548769861; // block 49148
    blockIndexPrev4.nBits = 0x1c24a16f;
    blockIndexPrev3.pprev = &blockIndexPrev4;
    CBlockIndex blockIndexPrev5 = CBlockIndex();
    blockIndexPrev5.nHeight = --nHeight;
    blockIndexPrev5.nTime = 1548769846; // block 49147
    blockIndexPrev5.nBits = 0x1c24604c;
    blockIndexPrev4.pprev = &blockIndexPrev5;
    CBlockIndex blockIndexPrev6 = CBlockIndex();
    blockIndexPrev6.nHeight = --nHeight;
    blockIndexPrev6.nTime = 1548769841; // block 49146
    blockIndexPrev6.nBits = 0x1c23ac2c;
    blockIndexPrev5.pprev = &blockIndexPrev6;
    CBlockIndex blockIndexPrev7 = CBlockIndex();
    blockIndexPrev7.nHeight = --nHeight;
    blockIndexPrev7.nTime = 1548769825; // block 49145
    blockIndexPrev7.nBits = 0x1c1dff55;
    blockIndexPrev6.pprev = &blockIndexPrev7;
    CBlockIndex blockIndexPrev8 = CBlockIndex();
    blockIndexPrev8.nHeight = --nHeight;
    blockIndexPrev8.nTime = 1548769499; // block 49144
    blockIndexPrev8.nBits = 0x1c1e379a;
    blockIndexPrev7.pprev = &blockIndexPrev8;
    CBlockIndex blockIndexPrev9 = CBlockIndex();
    blockIndexPrev9.nHeight = --nHeight;
    blockIndexPrev9.nTime = 1548769490; // block 49143
    blockIndexPrev9.nBits = 0x1c1e3ee8;
    blockIndexPrev8.pprev = &blockIndexPrev9;
    CBlockIndex blockIndexPrev10 = CBlockIndex();
    blockIndexPrev10.nHeight = --nHeight;
    blockIndexPrev10.nTime = 1548769488; // block 49142
    blockIndexPrev10.nBits = 0x1c1c004e;
    blockIndexPrev9.pprev = &blockIndexPrev10;
    CBlockIndex blockIndexPrev11 = CBlockIndex();
    blockIndexPrev11.nHeight = --nHeight;
    blockIndexPrev11.nTime = 1548769353; // block 49141
    blockIndexPrev11.nBits = 0x1c1bd628;
    blockIndexPrev10.pprev = &blockIndexPrev11;
    CBlockIndex blockIndexPrev12 = CBlockIndex();
    blockIndexPrev12.nHeight = --nHeight;
    blockIndexPrev12.nTime = 1548769324; // block 49140
    blockIndexPrev12.nBits = 0x1c1c798e;
    blockIndexPrev11.pprev = &blockIndexPrev12;
    CBlockIndex blockIndexPrev13 = CBlockIndex();
    blockIndexPrev13.nHeight = --nHeight;
    blockIndexPrev13.nTime = 1548769322; // block 49139
    blockIndexPrev13.nBits = 0x1c1b9912;
    blockIndexPrev12.pprev = &blockIndexPrev13;
    CBlockIndex blockIndexPrev14 = CBlockIndex();
    blockIndexPrev14.nHeight = --nHeight;
    blockIndexPrev14.nTime = 1548769149; // block 49138
    blockIndexPrev14.nBits = 0x1c1afd58;
    blockIndexPrev13.pprev = &blockIndexPrev14;
    CBlockIndex blockIndexPrev15 = CBlockIndex();
    blockIndexPrev15.nHeight = --nHeight;
    blockIndexPrev15.nTime = 1548769095; // block 49137
    blockIndexPrev15.nBits = 0x1c197d7f;
    blockIndexPrev14.pprev = &blockIndexPrev15;
    CBlockIndex blockIndexPrev16 = CBlockIndex();
    blockIndexPrev16.nHeight = --nHeight;
    blockIndexPrev16.nTime = 1548768982; // block 49136
    blockIndexPrev16.nBits = 0x1c1515cf;
    blockIndexPrev15.pprev = &blockIndexPrev16;
    CBlockIndex blockIndexPrev17 = CBlockIndex();
    blockIndexPrev17.nHeight = --nHeight;
    blockIndexPrev17.nTime = 1548768733; // block 49135
    blockIndexPrev17.nBits = 0x1c161f4a;
    blockIndexPrev16.pprev = &blockIndexPrev17;
    CBlockIndex blockIndexPrev18 = CBlockIndex();
    blockIndexPrev18.nHeight = --nHeight;
    blockIndexPrev18.nTime = 1548768673; // block 49134
    blockIndexPrev18.nBits = 0x1c1b8a12;
    blockIndexPrev17.pprev = &blockIndexPrev18;
    CBlockIndex blockIndexPrev19 = CBlockIndex();
    blockIndexPrev19.nHeight = --nHeight;
    blockIndexPrev19.nTime = 1548768666; // block 49133
    blockIndexPrev19.nBits = 0x1c1a7fe3;
    blockIndexPrev18.pprev = &blockIndexPrev19;
    CBlockIndex blockIndexPrev20 = CBlockIndex();
    blockIndexPrev20.nHeight = --nHeight;
    blockIndexPrev20.nTime = 1548768588; // block 49132
    blockIndexPrev20.nBits = 0x1c1a8d51;
    blockIndexPrev19.pprev = &blockIndexPrev20;
    CBlockIndex blockIndexPrev21 = CBlockIndex();
    blockIndexPrev21.nHeight = --nHeight;
    blockIndexPrev21.nTime = 1548768514; // block 49131
    blockIndexPrev21.nBits = 0x1c181035;
    blockIndexPrev20.pprev = &blockIndexPrev21;
    CBlockIndex blockIndexPrev22 = CBlockIndex();
    blockIndexPrev22.nHeight = --nHeight;
    blockIndexPrev22.nTime = 1548768370; // block 49130
    blockIndexPrev22.nBits = 0x1c16161c;
    blockIndexPrev21.pprev = &blockIndexPrev22;
    CBlockIndex blockIndexPrev23 = CBlockIndex();
    blockIndexPrev23.nHeight = --nHeight;
    blockIndexPrev23.nTime = 1548768203; // block 49129
    blockIndexPrev23.nBits = 0x1c143faa;
    blockIndexPrev22.pprev = &blockIndexPrev23;

    CBlockHeader blockHeader;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blockIndexLast, &blockHeader, chainParams->GetConsensus()), 0x1c24221e); // block #49153 has 0x1c24221e
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(CBaseChainParams::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(CBaseChainParams::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits = ~0x00800000;
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(CBaseChainParams::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(CBaseChainParams::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(CBaseChainParams::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p2 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p3 = &blocks[InsecureRandRange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

BOOST_AUTO_TEST_SUITE_END()
