#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include <validation.h>

class CBlockIndex;
class uint256;

static const unsigned int MODIFIER_INTERVAL = 60;
static const int MODIFIER_INTERVAL_RATIO = 3;

int64_t GetStakeModifierSelectionIntervalSection(int nSection);
int64_t GetStakeModifierSelectionInterval();
bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier);
uint256 stakeHash(unsigned int nTimeTx, CDataStream ss, unsigned int prevoutIndex, uint256 prevoutHash, unsigned int nTimeBlockFrom);
bool stakeTargetHit(uint256 hashProofOfStake, int64_t nValueIn, uint256 bnTargetPerCoinDay);
bool CheckStakeKernelHash(unsigned int nBits, const CBlock blockFrom, const CTransactionRef txPrev, const COutPoint prevout, unsigned int& nTimeTx, unsigned int nHashDrift, bool fCheck, uint256& hashProofOfStake, bool fPrintProofOfStake = false);
bool CheckProofOfStake(const CBlock block, uint256& hashProofOfStake);

#endif
