// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_commitment.h>

#include <chainparams.h>
#include <validation.h>

#include <evo/specialtx.h>

namespace llmq
{

CFinalCommitment::CFinalCommitment(const Consensus::LLMQParams& params, const uint256& _quorumHash) :
        llmqType(params.type),
        quorumHash(_quorumHash),
        signers(params.size),
        validMembers(params.size)
{
}

#define LogPrintfFinalCommitment(...) do { \
    LogPrintStr(strprintf("CFinalCommitment::%s -- %s", __func__, tinyformat::format(__VA_ARGS__))); \
} while(0)

bool CFinalCommitment::Verify(const std::vector<CDeterministicMNCPtr>& members, bool checkSigs) const
{
    if (nVersion == 0 || nVersion > CURRENT_VERSION) {
        return false;
    }

    if (!Params().GetConsensus().llmqs.count(llmqType)) {
        LogPrint(BCLog::LLMQ, "invalid llmqType=%d\n", llmqType);
        return false;
    }
    const auto& params = Params().GetConsensus().llmqs.at(llmqType);

    if (!VerifySizes(params)) {
        return false;
    }

    if (CountValidMembers() < params.minSize) {
        LogPrint(BCLog::LLMQ, "invalid validMembers count. validMembersCount=%d\n", CountValidMembers());
        return false;
    }
    if (CountSigners() < params.minSize) {
        LogPrint(BCLog::LLMQ, "invalid signers count. signersCount=%d\n", CountSigners());
        return false;
    }
    if (!quorumPublicKey.IsValid()) {
        LogPrint(BCLog::LLMQ, "invalid quorumPublicKey\n");
        return false;
    }
    if (quorumVvecHash.IsNull()) {
        LogPrint(BCLog::LLMQ, "invalid quorumVvecHash\n");
        return false;
    }
    if (!membersSig.IsValid()) {
        LogPrint(BCLog::LLMQ, "invalid membersSig\n");
        return false;
    }
    if (!quorumSig.IsValid()) {
        LogPrint(BCLog::LLMQ, "invalid vvecSig\n");
        return false;
    }

    for (size_t i = members.size(); i < params.size; i++) {
        if (validMembers[i]) {
            LogPrint(BCLog::LLMQ, "invalid validMembers bitset. bit %d should not be set\n", i);
            return false;
        }
        if (signers[i]) {
            LogPrint(BCLog::LLMQ, "invalid signers bitset. bit %d should not be set\n", i);
            return false;
        }
    }

    // sigs are only checked when the block is processed
    if (checkSigs) {
        uint256 commitmentHash = CLLMQUtils::BuildCommitmentHash(params.type, quorumHash, validMembers, quorumPublicKey, quorumVvecHash);

        std::vector<CBLSPublicKey> memberPubKeys;
        for (size_t i = 0; i < members.size(); i++) {
            if (!signers[i]) {
                continue;
            }
            memberPubKeys.emplace_back(members[i]->pdmnState->pubKeyOperator.Get());
        }

        if (!membersSig.VerifySecureAggregated(memberPubKeys, commitmentHash)) {
            LogPrint(BCLog::LLMQ, "invalid aggregated members signature\n");
            return false;
        }

        if (!quorumSig.VerifyInsecure(quorumPublicKey, commitmentHash)) {
            LogPrint(BCLog::LLMQ, "invalid quorum signature\n");
            return false;
        }
    }

    return true;
}

bool CFinalCommitment::VerifyNull() const
{
    if (!Params().GetConsensus().llmqs.count(llmqType)) {
        LogPrint(BCLog::LLMQ, "invalid llmqType=%d\n", llmqType);
        return false;
    }
    const auto& params = Params().GetConsensus().llmqs.at(llmqType);

    if (!IsNull() || !VerifySizes(params)) {
        return false;
    }

    return true;
}

bool CFinalCommitment::VerifySizes(const Consensus::LLMQParams& params) const
{
    if (signers.size() != params.size) {
        LogPrint(BCLog::LLMQ, "invalid signers.size=%d\n", signers.size());
        return false;
    }
    if (validMembers.size() != params.size) {
        LogPrint(BCLog::LLMQ, "invalid signers.size=%d\n", signers.size());
        return false;
    }
    return true;
}

bool CheckLLMQCommitment(const CTransaction& tx, const CBlockIndex* pindexPrev, TxValidationState& state)
{
    CFinalCommitmentTxPayload qcTx;
    if (!GetTxPayload(tx, qcTx)) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-qc-payload");
    }

    if (qcTx.nVersion == 0 || qcTx.nVersion > CFinalCommitmentTxPayload::CURRENT_VERSION) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-qc-version");
    }

    if (qcTx.nHeight != pindexPrev->nHeight + 1) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-qc-height");
    }

    if (!::BlockIndex().count(qcTx.commitment.quorumHash)) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-qc-quorum-hash");
    }

    const CBlockIndex* pindexQuorum = ::BlockIndex()[qcTx.commitment.quorumHash];

    if (pindexQuorum != pindexPrev->GetAncestor(pindexQuorum->nHeight)) {
        // not part of active chain
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-qc-quorum-hash");
    }

    if (!Params().GetConsensus().llmqs.count(qcTx.commitment.llmqType)) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-qc-type");
    }
    const auto& params = Params().GetConsensus().llmqs.at(qcTx.commitment.llmqType);

    if (qcTx.commitment.IsNull()) {
        if (!qcTx.commitment.VerifyNull()) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-qc-invalid-null");
        }
        return true;
    }

    auto members = CLLMQUtils::GetAllQuorumMembers(params.type, pindexQuorum);
    if (!qcTx.commitment.Verify(members, false)) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-qc-invalid");
    }

    return true;
}

} // namespace llmq
