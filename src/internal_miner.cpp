// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "internal_miner.h"

#include "arith_uint256.h"
#include "messages/message_encryption.h"
#include "streams.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "validation.h"
#include "pow.h"
#include "chainparams.h"

#include <boost/thread.hpp>

using namespace std;

namespace internal_miner
{


//////////////////////////////////////////////////////////////////////////////
//
// BitcoinMiner
//

//
// ScanHash scans nonces looking for a hash with at least some zero bits.
// The nonce is usually preserved between calls, but periodically or if the
// nonce is 0xffff0000 or above, the block is rebuilt and nNonce starts over at
// zero.
//
bool ScanHash(CMutableTransaction& txn, ExtNonce &extNonce, uint256 *phash, std::vector<unsigned char>& opReturnData)
{
    // Write the first 76 bytes of the block header to a double-SHA256 state.
//    CHash256 hasher;
//    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
//    ss << txn->GetHash();
//    hasher.Write((unsigned char*)&ss[0], 76);

    while (true)
    {
        ++extNonce.nonce;

        std::memcpy(opReturnData.data()+ENCR_MARKER_SIZE , &extNonce.tip_block_height, sizeof(uint32_t));
        std::memcpy(opReturnData.data()+ENCR_MARKER_SIZE+4, &extNonce.tip_block_hash, sizeof(uint32_t));
        std::memcpy(opReturnData.data()+ENCR_MARKER_SIZE+8, &extNonce.nonce, sizeof(uint32_t));

        CScript nScript;
        nScript << OP_RETURN << opReturnData;
        txn.vout[0].scriptPubKey = nScript;

        *phash = txn.GetHash();

        // Write the last 4 bytes of the block header (the nonce) to a copy of
        // the double-SHA256 state, and compute the result.
//        CHash256(hasher).Write((unsigned char*)&nNonce, 4).Finalize((unsigned char*)phash);

        // Return the nonce if the hash has at least some zero bits,
        // caller will check if it has enough to reach the target
        if (((uint16_t*)phash)[15] == 0x8000)
            return true;

        // If nothing found after trying for a while, return -1
        if ((extNonce.nonce & 0xfff) == 0)
            return false;
    }
    return false;
}

static CAmount getTxnCost(const CTransaction& txn) {
    //TODO: add checks for txn size() - for too small and too big
    const unsigned int txSize = txn.GetTotalSize();
    const CAmount feePerByte = 10;
    const CAmount cost = txSize * feePerByte;

    return cost;
}

static arith_uint256 getTarget(const CTransaction& txn)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);

    const CAmount blockReward = GetBlockSubsidy(chainActive.Height(), Params().GetConsensus());
    const CAmount txnCost = getTxnCost(txn);
    const uint32_t ratio = blockReward / txnCost;

    std::cout << "blockReward: " << blockReward << std::endl;
    std::cout << "txnCost: " << txnCost << std::endl;
    std::cout << "ratio: " << ratio << std::endl;

    unsigned int nBits = GetNextWorkRequired(pindexPrev, nullptr, Params().GetConsensus());
    arith_uint256 blockTarget = arith_uint256().SetCompact(nBits);
    arith_uint256 txnTarget = blockTarget * ratio;

    uint256 txnTargetUint256 = ArithToUint256(txnTarget);
    txnTargetUint256.flip_bit(PICO_BIT_POS);

    std::cout << "Target for block = " << blockTarget.ToString() << " = " << blockTarget.getdouble() << std::endl;
    std::cout << "Target for txn = "<< txnTarget.ToString() << " = " << txnTarget.getdouble() << std::endl;

    return UintToArith256(txnTargetUint256);
}

ExtNonce mineTransaction(CMutableTransaction& txn)
{
    int64_t nStart = GetTime();
    arith_uint256 hashTarget = getTarget(txn);
    LogPrintf("Hash target: %s\n", hashTarget.GetHex().c_str());

    if (txn.vout.size() != 1)
        return {0,0,0};

    std::vector<unsigned char> opReturn = txn.loadOpReturn();

    while (true) {

        CBlockIndex *prevBlock = chainActive.Tip();
        LogPrintf("block hash: %s, height: %u\n", prevBlock->GetBlockHash().ToString().c_str(), prevBlock->nHeight);
        uint256 hash;
        ExtNonce extNonce{(uint32_t)prevBlock->nHeight, (uint32_t)prevBlock->GetBlockHash().GetUint64(0), 0};

        while (true) {
            // Check if something found
            if (ScanHash(txn, extNonce, &hash, opReturn))
            {
                if (UintToArith256(txn.GetHash()) <= hashTarget)
                {
                    // Found a solution
                    LogPrintf("InternalMiner:\n");
                    LogPrintf("proof-of-work for transaction found  \n  hash: %s  \ntarget: %s\n", hash.GetHex().c_str(), hashTarget.GetHex().c_str());
                    LogPrintf("Block height:%u Block hash:%u nonce:%u\n", extNonce.tip_block_height, extNonce.tip_block_hash, extNonce.nonce);
                    LogPrintf("\nDuration: %ld seconds\n\n", GetTime() - nStart);
                    return extNonce;
                }
            }
//            printf("\rTransaction hash: %s", txn.GetHash().ToString().c_str());

            // Check for stop
            boost::this_thread::interruption_point();
            if (extNonce.nonce >= 0xffff0000)
                break;
            if (prevBlock != chainActive.Tip()) {
                printf("Internal miner: New block detected\n");
                break;
            }
        }
    }

    return {0,0,0};
}

bool verifyTransactionHash(const CTransaction& txn)
{
    arith_uint256 hashTarget = getTarget(txn);
    uint256 hash = txn.GetHash();
    std::vector<char> opReturn = txn.loadOpReturn();

    assert(opReturn.size() >= (unsigned int)(ENCR_MARKER_SIZE + 12));

    ExtNonce extNonce;
    std::memcpy(&extNonce.tip_block_height, opReturn.data()+ENCR_MARKER_SIZE, sizeof(uint32_t));
    std::memcpy(&extNonce.tip_block_hash, opReturn.data()+ENCR_MARKER_SIZE+4, sizeof(uint32_t));
    std::memcpy(&extNonce.nonce, opReturn.data()+ENCR_MARKER_SIZE+8, sizeof(uint32_t));

    CBlockIndex *prevBlock = chainActive.Tip();

    LogPrintf("proof-of-work verification  \n  hash: %s  \ntarget: %s\n", hash.GetHex().c_str(), hashTarget.GetHex().c_str());
    LogPrintf("  tip_block hash: %u\t tip_block height: %d\n", extNonce.tip_block_hash, extNonce.tip_block_height);
    LogPrintf("  tip_block hash: %u\t tip_block height: %d\n", (uint32_t)prevBlock->GetBlockHash().GetUint64(0), (uint32_t)prevBlock->nHeight);

    if (((uint16_t*)&hash)[15] != 0x8000)
        return false;
    if (UintToArith256(hash) > hashTarget)
        return false;
    if ((uint32_t)prevBlock->nHeight != extNonce.tip_block_height)
        return false;
    if ((uint32_t)prevBlock->GetBlockHash().GetUint64(0) != extNonce.tip_block_hash)
        return false;

    return true;
}

}
