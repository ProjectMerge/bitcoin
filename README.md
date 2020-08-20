![Merge Blockchain](https://i.imgur.com/vTFN9aJ.png)

Merge Core 0.20 integration/staging tree
========================================

https://projectmerge.org


What is Merge?
--------------

Merge provides a cutting-edge cryptocurrency, which acts as the engine and the connector to the products and solutions of Merge.
Merge had previously relied on the successful PIVX Core platform, providing a stable codebase; with access to new features such as
proof of stake and masternode functionality.

In wanting to stay ahead and competitive with other new cryptocurrencies; the decision was made to rebase our platform over the
latest Bitcoin codebase (0.20 at time of print), additionally being the first to migrate away from a PIVX-based core; whilst retaining
full compatibility with our existing client and featureset. This will eventually allow the use of late model features such as segwit,
bech32 addressing, headers-first synchronization; in addition to making our planned Ethereum-based sidechain with Smart Contract
functionality possible for use in the DeFi arena.

Currently, the codebase is in a testing phase - where we invite users with the knowhow to trial the new base. While there are some
bugs present still; they are usually just visual in nature, the blockchain/consensus base itself runs well. If you are looking for the
existing PIVX 3.0-series codebase; you will find it here (<a href="https://github.com/projectmerge/merge-legacy">merge-legacy</a>).


Compiling Merge
---------------

### Static compile (linux)

    git clone https://github.com/ProjectMerge/merge
    cd merge/depends
    make HOST=x86_64-linux-gnu
    cd ..
    ./autogen.sh
    ./configure --prefix=`pwd`/depends/x86_64-linux-gnu
    make


Credits
-------

Merge would like to thank the PIVX Project for use of its existing platform (and for being good sports) - https://github.com/PIVX-Project/PIVX,
and as well - the Qtum project (for use of its great UI) - https://github.com/qtumproject/qtum.


License
-------

Merge Core is released under the terms of the MIT license. See [COPYING](COPYING) for more information or see https://opensource.org/licenses/MIT.

