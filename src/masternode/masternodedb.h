// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2020 The Merge Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEDB_H
#define MASTERNODEDB_H

#include <base58.h>
#include <key.h>
#include <masternode/activemasternode.h>
#include <masternode/masternode.h>
#include <net.h>
#include <sync.h>
#include <util/system.h>
#include <validation.h>

void DumpMasternodes();

class CMasternodeMan;

/** Access to the MN database (mncache.dat)
 */
class CMasternodeDB {
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CMasternodeDB();
    bool Write(const CMasternodeMan& mnodemanToSave);
    ReadResult Read(CMasternodeMan& mnodemanToLoad, bool fDryRun = false);
};

#endif
