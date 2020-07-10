// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternode/masternodeman.h>

#include <addrman.h>
#include <chainparams.h>
#include <masternode/masternode-helpers.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternode.h>
#include <masternode/spork.h>
#include <net_processing.h>
#include <netmessagemaker.h>
#include <shutdown.h>
#include <util/system.h>

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#define MN_WINNER_MINIMUM_AGE 4000

using namespace std;

/** Masternode manager */
CMasternodeMan mnodeman;

struct CompareLastPaid {
    bool operator()(const std::pair<int64_t, CTxIn>& t1,
        const std::pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const std::pair<int64_t, CTxIn>& t1,
        const std::pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN {
    bool operator()(const std::pair<int64_t, CMasternode>& t1,
        const std::pair<int64_t, CMasternode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CMasternodeDB
//

CMasternodeDB::CMasternodeDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "MasternodeCache";
}

bool CMasternodeDB::Write(const CMasternodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssMasternodes(SER_DISK, CLIENT_VERSION);
    ssMasternodes << strMagicMessage; // masternode cache file specific magic message
    ssMasternodes << MakeSpan(Params().MessageStart()); // network specific magic number
    ssMasternodes << mnodemanToSave;
    uint256 hash = Hash(ssMasternodes.begin(), ssMasternodes.end());
    ssMasternodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssMasternodes;
    } catch (const std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint(BCLog::MASTERNODE, "Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint(BCLog::MASTERNODE, "  %s\n", mnodemanToSave.ToString());

    return true;
}

CMasternodeDB::ReadResult CMasternodeDB::Read(CMasternodeMan& mnodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();

    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    std::vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (const std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssMasternodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssMasternodes.begin(), ssMasternodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..
        ssMasternodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssMasternodes >> MakeSpan(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CMasternodeMan object
        ssMasternodes >> mnodemanToLoad;
    } catch (const std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint(BCLog::MASTERNODE, "Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint(BCLog::MASTERNODE, "  %s\n", mnodemanToLoad.ToString());
    if (!fDryRun) {
        LogPrint(BCLog::MASTERNODE, "Masternode manager - cleaning....\n");
        mnodemanToLoad.CheckAndRemove(true);
        LogPrint(BCLog::MASTERNODE, "Masternode manager - result:\n");
        LogPrint(BCLog::MASTERNODE, "  %s\n", mnodemanToLoad.ToString());
    }

    return Ok;
}

void DumpMasternodes()
{
    int64_t nStart = GetTimeMillis();

    CMasternodeDB mndb;
    CMasternodeMan tempMnodeman;

    LogPrint(BCLog::MASTERNODE, "Verifying mncache.dat format...\n");
    CMasternodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CMasternodeDB::FileError)
        LogPrint(BCLog::MASTERNODE, "Missing masternode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CMasternodeDB::Ok) {
        LogPrint(BCLog::MASTERNODE, "Error reading mncache.dat: ");
        if (readResult == CMasternodeDB::IncorrectFormat)
            LogPrint(BCLog::MASTERNODE, "magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint(BCLog::MASTERNODE, "file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint(BCLog::MASTERNODE, "Writing info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint(BCLog::MASTERNODE, "Masternode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CMasternodeMan::CMasternodeMan()
{
    nDsqCount = 0;
}

bool CMasternodeMan::Add(CMasternode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CMasternode* pmn = Find(mn.vin);
    if (pmn == nullptr) {
        LogPrint(BCLog::MASTERNODE, "CMasternodeMan: Adding new Masternode %s - %i now\n", mn.vin.prevout.hash.ToString(), size() + 1);
        vMasternodes.push_back(mn);
        return true;
    }

    return false;
}

void CMasternodeMan::AskForMN(CNode* pnode, CTxIn& vin, CConnman& connman)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasternodeListEntry.find(vin.prevout);
    if (i != mWeAskedForMasternodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t)
            return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp
    //! LogPrint(BCLog::MASTERNODE, "CMasternodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make("dseg", vin));
    int64_t askAgain = GetTime() + MASTERNODE_MIN_MNP_SECONDS;
    mWeAskedForMasternodeListEntry[vin.prevout] = askAgain;
}

void CMasternodeMan::Check()
{
    LOCK(cs);

    for (CMasternode& mn : vMasternodes) {
        mn.Check();
    }
}

void CMasternodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    std::vector<CMasternode>::iterator it = vMasternodes.begin();
    while (it != vMasternodes.end()) {
        if ((*it).activeState == CMasternode::MASTERNODE_REMOVE || (*it).activeState == CMasternode::MASTERNODE_VIN_SPENT || (forceExpiredRemoval && (*it).activeState == CMasternode::MASTERNODE_EXPIRED) || (*it).protocolVersion < masternodePayments.GetMinMasternodePaymentsProto()) {
            LogPrint(BCLog::MASTERNODE, "CMasternodeMan: Removing inactive Masternode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new mnb
            std::map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
            while (it3 != mapSeenMasternodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    masternodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenMasternodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this masternode again if we see another ping
            std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.begin();
            while (it2 != mWeAskedForMasternodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForMasternodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vMasternodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Masternode list
    std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForMasternodeList.begin();
    while (it1 != mAskedUsForMasternodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForMasternodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Masternode list
    it1 = mWeAskedForMasternodeList.begin();
    while (it1 != mWeAskedForMasternodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForMasternodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Masternodes we've asked for
    std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.begin();
    while (it2 != mWeAskedForMasternodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForMasternodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenMasternodeBroadcast
    std::map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
    while (it3 != mapSeenMasternodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMasternodeBroadcast.erase(it3++);
            masternodeSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenMasternodePing
    std::map<uint256, CMasternodePing>::iterator it4 = mapSeenMasternodePing.begin();
    while (it4 != mapSeenMasternodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMasternodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CMasternodeMan::Clear()
{
    LOCK(cs);
    vMasternodes.clear();
    mAskedUsForMasternodeList.clear();
    mWeAskedForMasternodeList.clear();
    mWeAskedForMasternodeListEntry.clear();
    mapSeenMasternodeBroadcast.clear();
    mapSeenMasternodePing.clear();
    nDsqCount = 0;
}

int CMasternodeMan::stable_size()
{
    int nStable_size = 0;
    int nMinProtocol = PROTOCOL_VERSION;
    int64_t nMasternode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nMasternode_Age = 0;

    for (CMasternode& mn : vMasternodes) {
        if (mn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (sporkManager.IsSporkActive(Spork::SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
            nMasternode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nMasternode_Age) < nMasternode_Min_Age) {
                continue; // Skip masternodes younger than (default) 8000 sec (MUST be > MASTERNODE_REMOVAL_SECONDS)
            }
        }
        mn.Check();
        if (!mn.IsEnabled())
            continue; // Skip not-enabled masternodes

        nStable_size++;
    }

    return nStable_size;
}

int CMasternodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

    for (CMasternode& mn : vMasternodes) {
        mn.Check();
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled())
            continue;
        i++;
    }

    return i;
}

void CMasternodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

    for (CMasternode& mn : vMasternodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
        case 1:
            ipv4++;
            break;
        case 2:
            ipv6++;
            break;
        case 3:
            onion++;
            break;
        }
    }
}

void CMasternodeMan::DsegUpdate(CNode* pnode, CConnman& connman)
{
    LOCK(cs);

    if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
        std::map<CNetAddr, int64_t>::iterator it = mWeAskedForMasternodeList.find(pnode->addr);
        if (it != mWeAskedForMasternodeList.end()) {
            if (GetTime() < (*it).second) {
                LogPrint(BCLog::MASTERNODE, "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                return;
            }
        }
    }

    connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make("dseg", CTxIn()));
    int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
    mWeAskedForMasternodeList[pnode->addr] = askAgain;

    LogPrint(BCLog::MASTERNODE, "CMasternodeMan::DsegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CMasternode* CMasternodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    for (CMasternode& mn : vMasternodes) {
        payee2 = GetScriptForDestination(PKHash(mn.pubKeyCollateralAddress));
        if (payee2 == payee)
            return &mn;
    }
    return nullptr;
}

CMasternode* CMasternodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    for (CMasternode& mn : vMasternodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return nullptr;
}

CMasternode* CMasternodeMan::Find(const CPubKey& pubKeyMasternode)
{
    LOCK(cs);

    for (CMasternode& mn : vMasternodes) {
        if (mn.pubKeyMasternode == pubKeyMasternode)
            return &mn;
    }
    return nullptr;
}

//
// Deterministically select the oldest/best masternode to pay on the network
//
CMasternode* CMasternodeMan::GetNextMasternodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CMasternode* pBestMasternode = nullptr;
    std::vector<std::pair<int64_t, CTxIn>> vecMasternodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    for (CMasternode& mn : vMasternodes) {
        mn.Check();
        if (!mn.IsEnabled())
            continue;

        // check protocol version
        if (mn.protocolVersion < masternodePayments.GetMinMasternodePaymentsProto())
            continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (masternodePayments.IsScheduled(mn, nBlockHeight))
            continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime())
            continue;

        //make sure it has as many confirmations as there are masternodes
        if (mn.GetMasternodeInputAge() < nMnCount)
            continue;

        vecMasternodeLastPaid.push_back(std::make_pair(mn.SecondsSincePayment(), mn.vin));
    }

    nCount = (int)vecMasternodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3)
        return GetNextMasternodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecMasternodeLastPaid.rbegin(), vecMasternodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled() / 10;
    int nCountTenth = 0;
    arith_uint256 nHigh;
    for (const auto s : vecMasternodeLastPaid) {
        CMasternode* pmn = Find(s.second);
        if (!pmn)
            break;
        arith_uint256 n = UintToArith256(pmn->CalculateScore(1, nBlockHeight - 100));
        if (n > nHigh) {
            nHigh = n;
            pBestMasternode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork)
            break;
    }
    return pBestMasternode;
}

CMasternode* CMasternodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint(BCLog::MASTERNODE, "CMasternodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1)
        return nullptr;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint(BCLog::MASTERNODE, "CMasternodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    for (CMasternode& mn : vMasternodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled())
            continue;
        found = false;
        for (CTxIn& usedVin : vecToExclude) {
            if (mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if (found)
            continue;
        if (--rand < 1) {
            return &mn;
        }
    }

    return nullptr;
}

CMasternode* CMasternodeMan::GetCurrentMasterNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CMasternode* winner = nullptr;

    // scan for winner
    for (CMasternode& mn : vMasternodes) {
        mn.Check();
        if (mn.protocolVersion < minProtocol || !mn.IsEnabled())
            continue;

        // calculate the score for each Masternode
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        int64_t n2 = UintToArith256(n).GetCompact(false);

        // determine the winner
        if (n2 > score) {
            score = n2;
            winner = &mn;
        }
    }

    return winner;
}

int CMasternodeMan::GetMasternodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CTxIn>> vecMasternodeScores;
    int64_t nMasternode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nMasternode_Age = 0;

    //make sure we know about this block
    uint256 hash;
    if (!GetBlockHash(hash, nBlockHeight))
        return -1;

    // scan for winner
    for (CMasternode& mn : vMasternodes) {
        if (mn.protocolVersion < minProtocol) {
            LogPrint(BCLog::MASTERNODE, "Skipping Masternode with obsolete version %d\n", mn.protocolVersion);
            continue; // Skip obsolete versions
        }

        if (sporkManager.IsSporkActive(Spork::SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
            nMasternode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nMasternode_Age) < nMasternode_Min_Age) {
                LogPrint(BCLog::MASTERNODE, "Skipping just activated Masternode. Age: %ld\n", nMasternode_Age);
                continue; // Skip masternodes younger than (default) 1 hour
            }
        }
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled())
                continue;
        }
        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = UintToArith256(n).GetCompact(false);

        vecMasternodeScores.push_back(std::make_pair(n2, mn.vin));
    }

    sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    for (const auto s : vecMasternodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<std::pair<int, CMasternode>> CMasternodeMan::GetMasternodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<std::pair<int64_t, CMasternode>> vecMasternodeScores;
    std::vector<std::pair<int, CMasternode>> vecMasternodeRanks;

    //make sure we know about this block
    uint256 hash = uint256();
    if (!GetBlockHash(hash, nBlockHeight))
        return vecMasternodeRanks;

    // scan for winner
    for (CMasternode& mn : vMasternodes) {
        mn.Check();

        if (mn.protocolVersion < minProtocol)
            continue;

        if (!mn.IsEnabled()) {
            vecMasternodeScores.push_back(std::make_pair(9999, mn));
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = UintToArith256(n).GetCompact(false);

        vecMasternodeScores.push_back(std::make_pair(n2, mn));
    }

    sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareScoreMN());

    int rank = 0;
    for (const auto s : vecMasternodeScores) {
        rank++;
        vecMasternodeRanks.push_back(std::make_pair(rank, s.second));
    }

    return vecMasternodeRanks;
}

CMasternode* CMasternodeMan::GetMasternodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CTxIn>> vecMasternodeScores;

    // scan for winner
    for (CMasternode& mn : vMasternodes) {
        if (mn.protocolVersion < minProtocol)
            continue;
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled())
                continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = UintToArith256(n).GetCompact(false);

        vecMasternodeScores.push_back(std::make_pair(n2, mn.vin));
    }

    sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    for (const auto s : vecMasternodeScores) {
        rank++;
        if (rank == nRank) {
            return Find(s.second);
        }
    }

    return nullptr;
}

void CMasternodeMan::ProcessMasternodeConnections(CConnman& connman)
{
    //we don't care about this for regtest
    if (Params().NetworkIDString() == CBaseChainParams::REGTEST)
        return;

    connman.ForEachNode([](CNode* pnode) {
        if (pnode->fMasternode) {
            LogPrintf("CMasternodeMan::ProcessMasternodeConnections -- removing node: peer=%d addr=%s nRefCount=%d fNetworkNode=%d fInbound=%d fMasternode=%d\n",
                pnode->GetId(), pnode->addr.ToString(), pnode->GetRefCount(), pnode->fNetworkNode, pnode->fInbound, pnode->fMasternode);
            pnode->fDisconnect = true;
        }
    });
}

void CMasternodeMan::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if (!masternodeSync.IsBlockchainSynced())
        return;

    LOCK(cs_process_message);

    if (strCommand == "mnb") {

        CMasternodeBroadcast mnb;
        vRecv >> mnb;

        if (mapSeenMasternodeBroadcast.count(mnb.GetHash())) { //seen
            masternodeSync.AddedMasternodeList(mnb.GetHash());
            return;
        }
        mapSeenMasternodeBroadcast.insert(std::make_pair(mnb.GetHash(), mnb));

        int nDoS = 0;
        if (!mnb.CheckAndUpdate(nDoS, connman)) {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);

            //failed
            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the Masternode
        //  - this is expensive, so it's only done once per Masternode
        if (!masternodeSigner.IsVinAssociatedWithPubkey(mnb.vin, mnb.pubKeyCollateralAddress)) {
            LogPrint(BCLog::MASTERNODE, "CMasternodeMan::ProcessMessage() : mnb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33);
            return;
        }

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()
        if (mnb.CheckInputsAndAdd(nDoS, connman)) {
            masternodeSync.AddedMasternodeList(mnb.GetHash());
        } else {
            LogPrint(BCLog::MASTERNODE, "mnb - Rejected Masternode entry %s\n", mnb.vin.prevout.hash.ToString());

            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "mnp") {
        CMasternodePing mnp;
        vRecv >> mnp;

        LogPrint(BCLog::MASTERNODE, "mnp - Masternode ping, vin: %s\n", mnp.vin.prevout.hash.ToString());

        if (mapSeenMasternodePing.count(mnp.GetHash()))
            return; //seen
        mapSeenMasternodePing.insert(std::make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if (mnp.CheckAndUpdate(nDoS, connman))
            return;

        if (nDoS > 0) {
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            CMasternode* pmn = Find(mnp.vin);
            if (pmn != nullptr)
                return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a masternode entry once
        AskForMN(pfrom, mnp.vin, connman);
    }

    else if (strCommand == "dseg") { //Get Masternode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && Params().NetworkIDString() == "main") {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForMasternodeList.find(pfrom->addr);
                if (i != mAskedUsForMasternodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        LogPrint(BCLog::MASTERNODE, "CMasternodeMan::ProcessMessage() : dseg - peer already asked me for the list\n");
                        Misbehaving(pfrom->GetId(), 34);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
                mAskedUsForMasternodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        for (CMasternode& mn : vMasternodes) {
            if (mn.addr.IsRFC1918())
                continue; //local network

            if (mn.IsEnabled()) {
                LogPrint(BCLog::MASTERNODE, "dseg - Sending Masternode entry - %s \n", mn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == mn.vin) {
                    CMasternodeBroadcast mnb = CMasternodeBroadcast(mn);
                    uint256 hash = mnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_MASTERNODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenMasternodeBroadcast.count(hash))
                        mapSeenMasternodeBroadcast.insert(std::make_pair(hash, mnb));

                    if (vin == mn.vin) {
                        LogPrint(BCLog::MASTERNODE, "dseg - Sent 1 Masternode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make("ssc", MASTERNODE_SYNC_LIST, nInvCount));
            LogPrint(BCLog::MASTERNODE, "dseg - Sent %d Masternode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    }

    else if (strCommand == "dsee") {

        if (sporkManager.IsSporkActive(Spork::SPORK_10_MASTERNODE_PAY_UPDATED_NODES))
            return;

        CTxIn vin;
        CService addr;
        CPubKey pubkey;
        CPubKey pubkey2;
        std::vector<unsigned char> vchSig;
        int64_t sigTime;
        int count;
        int current;
        int64_t lastUpdated;
        int protocolVersion;
        CScript donationAddress;
        int donationPercentage;
        std::string strMessage;

        vRecv >> vin >> addr >> vchSig >> sigTime >> pubkey >> pubkey2 >> count >> current >> lastUpdated >> protocolVersion >> donationAddress >> donationPercentage;

        // make sure signature isn't in the future (past is OK)
        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrint(BCLog::MASTERNODE, "%s::ProcessMessage() : dsee - Signature rejected, too far into the future %s\n", __func__, vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

        strMessage = addr.ToString() + std::to_string(sigTime) + vchPubKey + vchPubKey2 + std::to_string(protocolVersion) + donationAddress.ToString() + std::to_string(donationPercentage);

        if (protocolVersion < masternodePayments.GetMinMasternodePaymentsProto()) {
            LogPrintf("CMasternodeMan::ProcessMessage() : dsee - ignoring outdated Masternode %s protocol version %d < %d\n", vin.prevout.hash.ToString(), protocolVersion, masternodePayments.GetMinMasternodePaymentsProto());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        CScript pubkeyScript;
        pubkeyScript = GetScriptForDestination(PKHash(pubkey));

        if (pubkeyScript.size() != 25) {
            LogPrint(BCLog::MASTERNODE, "%s::ProcessMessage() : dsee - pubkey the wrong size\n", __func__);
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        CScript pubkeyScript2;
        pubkeyScript2 = GetScriptForDestination(PKHash(pubkey2));
        if (pubkeyScript2.size() != 25) {
            LogPrint(BCLog::MASTERNODE, "%s::ProcessMessage() : dsee - pubkey2 the wrong size\n", __func__);
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if (!vin.scriptSig.empty()) {
            LogPrint(BCLog::MASTERNODE, "%s::ProcessMessage() : dsee - Ignore Not Empty ScriptSig %s\n", __func__, vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        std::string errorMessage = "";
        if (!masternodeSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage)) {
            LogPrint(BCLog::MASTERNODE, "%s::ProcessMessage() : dsee - Got bad Masternode address signature\n", __func__);
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        //search existing Masternode list, this is where we update existing Masternodes with new dsee broadcasts
        CMasternode* pmn = this->Find(vin);
        if (pmn != NULL) {
            // count == -1 when it's a new entry
            //   e.g. We don't want the entry relayed/time updated when we're syncing the list
            // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
            //   after that they just need to match
            if (count == -1 && pmn->pubKeyCollateralAddress == pubkey && (GetAdjustedTime() - pmn->nLastDsee > MASTERNODE_MIN_MNB_SECONDS)) {
                if (pmn->protocolVersion > GETHEADERS_VERSION && sigTime - pmn->lastPing.sigTime < MASTERNODE_MIN_MNB_SECONDS)
                    return;
                if (pmn->nLastDsee < sigTime) { //take the newest entry
                    LogPrint(BCLog::MASTERNODE, "%s::ProcessMessage() :  dsee - Got updated entry for %s\n", __func__, vin.prevout.hash.ToString());
                    if (pmn->protocolVersion < GETHEADERS_VERSION) {
                        pmn->pubKeyMasternode = pubkey2;
                        pmn->sigTime = sigTime;
                        pmn->sig = vchSig;
                        pmn->protocolVersion = protocolVersion;
                        pmn->addr = addr;
                        //fake ping
                        pmn->lastPing = CMasternodePing(vin);
                    }
                    pmn->nLastDsee = sigTime;
                    pmn->Check();
                    if (pmn->IsEnabled()) {
                        std::vector<CNode*> vNodesCopy = connman.CopyNodeVector();
                        for (CNode* pnode : vNodesCopy) {
                            if (pnode->nVersion >= masternodePayments.GetMinMasternodePaymentsProto())
                                connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make("dsee", vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage));
                        }
                    }
                }
            }

            return;
        }

        static std::map<COutPoint, CPubKey> mapSeenDsee;
        if (mapSeenDsee.count(vin.prevout) && mapSeenDsee[vin.prevout] == pubkey) {
            LogPrint(BCLog::MASTERNODE, "%s::ProcessMessage() : dsee - already seen this vin %s\n", __func__, vin.prevout.ToString());
            return;
        }
        mapSeenDsee.insert(std::make_pair(vin.prevout, pubkey));
        // make sure the vout that was signed is related to the transaction that spawned the Masternode
        //  - this is expensive, so it's only done once per Masternode
        if (!masternodeSigner.IsVinAssociatedWithPubkey(vin, pubkey)) {
            LogPrint(BCLog::MASTERNODE, "%s::ProcessMessage() : dsee - Got mismatched pubkey and vin\n", __func__);
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        LogPrint(BCLog::MASTERNODE, "%s::ProcessMessage() : dsee - Got NEW OLD Masternode entry %s\n", vin.prevout.hash.ToString());

        {
            COutPoint mnConfirms(vin.prevout.hash, vin.prevout.n);
            if (GetUTXOConfirmations(mnConfirms) < MASTERNODE_MIN_CONFIRMATIONS) {
                LogPrintf("CMasternodeMan::ProcessMessage() : dsee - Input must have least %d confirmations\n", MASTERNODE_MIN_CONFIRMATIONS);
                Misbehaving(pfrom->GetId(), 20);
                return;
            }

            // verify that sig time is legit in past
            // should be at least not earlier than block when 10000 MERGE tx got MASTERNODE_MIN_CONFIRMATIONS
            uint256 hashBlock = uint256();
            CTransactionRef tx2;
            GetTransaction(vin.prevout.hash, tx2, Params().GetConsensus(), hashBlock);
            BlockMap::iterator mi = ::BlockIndex().find(hashBlock);
            if (mi != ::BlockIndex().end() && (*mi).second) {
                CBlockIndex* pMNIndex = (*mi).second;
                CBlockIndex* pConfIndex = ::ChainActive()[pMNIndex->nHeight + MASTERNODE_MIN_CONFIRMATIONS - 1];
                if (pConfIndex->GetBlockTime() > sigTime) {
                    LogPrint(BCLog::MASTERNODE, "%s::ProcessMessage() : mnb - Bad sigTime %d for Masternode %s (%i conf block is at %d)\n",
                        __func__, sigTime, vin.prevout.hash.ToString(), MASTERNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
                    return;
                }
            }

            // add Masternode
            CMasternode mn = CMasternode();
            mn.addr = addr;
            mn.vin = vin;
            mn.pubKeyCollateralAddress = pubkey;
            mn.sig = vchSig;
            mn.sigTime = sigTime;
            mn.pubKeyMasternode = pubkey2;
            mn.protocolVersion = protocolVersion;
            // fake ping
            mn.lastPing = CMasternodePing(vin);
            mn.Check(true);

            if (protocolVersion < GETHEADERS_VERSION) {
                LogPrint(BCLog::MASTERNODE, "dsee - Accepted OLD Masternode entry %i %i\n", count, current);
                Add(mn);
            }

            if (mn.IsEnabled()) {
                std::vector<CNode*> vNodesCopy = connman.CopyNodeVector();
                for (CNode* pnode : vNodesCopy) {
                    if (pnode->nVersion >= masternodePayments.GetMinMasternodePaymentsProto())
                        connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make("dsee", vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage));
                }
            }
        }
    }

    else if (strCommand == "dseep") {

        if (sporkManager.IsSporkActive(Spork::SPORK_10_MASTERNODE_PAY_UPDATED_NODES))
            return;

        CTxIn vin;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        bool stop;
        vRecv >> vin >> vchSig >> sigTime >> stop;

        //! LogPrint(BCLog::MASTERNODE, "dseep - Received: vin: %s sigTime: %lld stop: %s\n", vin.ToString().c_str(), sigTime, stop ? "true" : "false");

        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrint(BCLog::MASTERNODE, "dseep - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        if (sigTime <= GetAdjustedTime() - 60 * 60) {
            LogPrint(BCLog::MASTERNODE, "dseep - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasternodeListEntry.find(vin.prevout);
        if (i != mWeAskedForMasternodeListEntry.end()) {
            int64_t t = (*i).second;
            if (GetTime() < t)
                return; // we've asked recently
        }

        // see if we have this Masternode
        CMasternode* pmn = this->Find(vin);
        if (pmn != NULL && pmn->protocolVersion >= masternodePayments.GetMinMasternodePaymentsProto()) {
            // LogPrint("masternode","dseep - Found corresponding mn for vin: %s\n", vin.ToString().c_str());
            // take this only if it's newer
            if (sigTime - pmn->nLastDseep > MASTERNODE_MIN_MNP_SECONDS) {
                std::string strMessage = pmn->addr.ToString() + boost::lexical_cast<std::string>(sigTime) + boost::lexical_cast<std::string>(stop);

                std::string errorMessage = "";
                if (!masternodeSigner.VerifyMessage(pmn->pubKeyMasternode, vchSig, strMessage, errorMessage)) {
                    LogPrint(BCLog::MASTERNODE, "dseep - Got bad Masternode address signature %s \n", vin.prevout.hash.ToString());
                    //Misbehaving(pfrom->GetId(), 100);
                    return;
                }

                // fake ping for v11 masternodes, ignore for v12
                if (pmn->protocolVersion < GETHEADERS_VERSION)
                    pmn->lastPing = CMasternodePing(vin);
                pmn->nLastDseep = sigTime;
                pmn->Check();
                if (pmn->IsEnabled()) {
                    LogPrint(BCLog::MASTERNODE, "dseep - relaying %s \n", vin.prevout.hash.ToString());
                    std::vector<CNode*> vNodesCopy = connman.CopyNodeVector();
                    for (CNode* pnode : vNodesCopy)
                        if (pnode->nVersion >= masternodePayments.GetMinMasternodePaymentsProto())
                            connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make("dseep", vin, vchSig, sigTime, stop));
                }
            }
            return;
        }

        // LogPrint(BCLog::MASTERNODE, "dseep - Couldn't find Masternode entry %s peer=%i\n", vin.prevout.hash.ToString(), pfrom->GetId());

        AskForMN(pfrom, vin, connman);
    }

    /*
     * END OF "REMOVE"
     */
}
void CMasternodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    std::vector<CMasternode>::iterator it = vMasternodes.begin();
    while (it != vMasternodes.end()) {
        if ((*it).vin == vin) {
            LogPrint(BCLog::MASTERNODE, "CMasternodeMan: Removing Masternode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);
            vMasternodes.erase(it);
            break;
        }
        ++it;
    }
}

void CMasternodeMan::UpdateMasternodeList(CMasternodeBroadcast mnb, CConnman& connman)
{
    LOCK(cs);
    mapSeenMasternodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenMasternodeBroadcast.insert(std::make_pair(mnb.GetHash(), mnb));

    LogPrint(BCLog::MASTERNODE, "CMasternodeMan::UpdateMasternodeList() -- masternode=%s\n", mnb.vin.prevout.ToString());

    CMasternode* pmn = Find(mnb.vin);
    if (pmn == nullptr) {
        CMasternode mn(mnb);
        if (Add(mn)) {
            masternodeSync.AddedMasternodeList(mnb.GetHash());
        }
    } else if (pmn->UpdateFromNewBroadcast(mnb, connman)) {
        masternodeSync.AddedMasternodeList(mnb.GetHash());
    }
}

std::string CMasternodeMan::ToString() const
{
    std::ostringstream info;

    info << "Masternodes: " << (int)vMasternodes.size() << ", peers who asked us for Masternode list: " << (int)mAskedUsForMasternodeList.size() << ", peers we asked for Masternode list: " << (int)mWeAskedForMasternodeList.size() << ", entries in Masternode list we asked for: " << (int)mWeAskedForMasternodeListEntry.size();

    return info.str();
}

void ThreadCheckMasternodes(CConnman* connman)
{
    unsigned int c = 0;

    //! spin until chain synced
    while (!masternodeSync.IsBlockchainSynced() && !ShutdownRequested()) { usleep(5000); };

    while (true)
    {
        MilliSleep(1000);

        // try to sync from all available nodes, one step at a time
        masternodeSync.Process(*connman);

        if (masternodeSync.IsBlockchainSynced()) {
            c++;

            // check if we should activate or ping every few minutes,
            // start right after sync is considered to be done
            if (c % MASTERNODE_PING_SECONDS == 1)
                activeMasternode.ManageStatus(*connman);

            if (c % 60 == 0) {
                mnodeman.CheckAndRemove();
                mnodeman.ProcessMasternodeConnections(*connman);
                masternodePayments.CleanPaymentList();
            }
        }
    }
}
