#!/usr/bin/env python3

"""
Test message transactions functionalities (createmsgtranssaction, handling blocks with incorrect
msg transactions)
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.messengertools import get_msgs_for_node, check_msg_txn, get_low_32_bits, get_txn_cost, \
    MSG_TXN_ACCEPTED_DEPTH
from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import uint256_from_str, hex_str_to_bytes, CTransaction, CTxOut, CTxIn, COutPoint, ToHex
from test_framework.script import CScript, OP_RETURN
from test_framework.util import assert_equal, connect_nodes_bi, disconnect_nodes
import struct
import copy
import time


def tx_hash(tx):
    return "%s..." % tx[0: 10]


class MessengerTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.extra_args = [["-txindex"]] * self.num_nodes
        self.setup_clean_chain = True

    def init_test(self):
        self.nodeA = self.nodes[0]
        self.nodeB = self.nodes[1]
        self.nodeC = self.nodes[2]
        self.nodeD = self.nodes[3]
        self.nodeA.generate(nblocks=1000)

    def sync_group_till_block(self, nodes, tip):
        nodes[0].waitforblock(tip)
        self.sync_all([nodes])

    def sync_all_till_block(self, tip):
        self.nodeA.waitforblock(tip)
        self.sync_all()

    def sync_all_till_txns_in_mempool(self, num_txns):
        timeout = 30
        wait = 1
        stop_time = time.time() + timeout
        while time.time() <= stop_time:
            if len(self.nodeA.getrawmempool()) == num_txns:
                break
            time.sleep(wait)
        else:
            raise AssertionError("some msg txns did not enter mempools")
        self.sync_all()

    def create_block_with_msgs(self, txns=[]):
        coinbase = create_coinbase(self.nodeA.getblockcount() + 1)
        block = create_block(int(self.nodeA.getbestblockhash(), 16),
                             coinbase,
                             self.nodeA.getblock(self.nodeA.getbestblockhash())['time'] + 1)
        block.vtx.extend(txns)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.rehash()
        block.solve()

        return block

    def get_target(self, txn):
        block_subsidy = 5000000000
        txn_cost = get_txn_cost(txn)

        ratio = block_subsidy // txn_cost
        block_target = uint256_from_str(
            hex_str_to_bytes("0000000000ffff00000000000000000000000000000000000000000000000000")[::-1])

        target = block_target * ratio
        return target

    def create_op_return_data(self, tip_height, tip_hash, nonce):
        op_return_data = bytearray("MSGFREE:", "utf-8")
        op_return_data += struct.pack("<I", tip_height)
        op_return_data += struct.pack("<I", tip_hash)
        op_return_data += struct.pack("<I", nonce)  # placeholder for additional info
        op_return_data += bytearray(1200)  # placeholder for encrypted msg
        return op_return_data

    def mine_msg_txn(self, tip_height, tip_hash):
        nonce = 0
        op_return_data = self.create_op_return_data(tip_height, tip_hash, nonce)

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(0, 0xfffffffe), b"", 0xffffffff))
        tx.vout.append(CTxOut(0, CScript([OP_RETURN, op_return_data])))
        tx.rehash()

        target = self.get_target(tx)

        while tx.sha256 ^ 0x8000000000000000000000000000000000000000000000000000000000000000 > target:
            nonce += 1
            op_return_data[16:20] = struct.pack("<I", nonce)
            tx.vout[0] = CTxOut(0, CScript([OP_RETURN, op_return_data]))
            tx.rehash()
        return tx

    def mine_msg_txn_incorrectly(self, tip_height, tip_hash):
        nonce = 0
        op_return_data = self.create_op_return_data(tip_height, tip_hash, nonce)

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(0, 0xfffffffe), b"", 0xffffffff))
        tx.vout.append(CTxOut(0, CScript([OP_RETURN, op_return_data])))
        tx.rehash()

        lower_bound = self.get_target(tx)
        upper_bound = uint256_from_str(
            hex_str_to_bytes("00FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")[::-1])

        while tx.sha256 ^ 0x8000000000000000000000000000000000000000000000000000000000000000 <= lower_bound or \
            tx.sha256 ^ 0x8000000000000000000000000000000000000000000000000000000000000000 > upper_bound:
                nonce += 1
                op_return_data[16:20] = struct.pack("<I", nonce)
                tx.vout[0] = CTxOut(0, CScript([OP_RETURN, op_return_data]))
                tx.rehash()

        return tx

    def test_mining_msg_txns(self):
        """Creates three msg txns, checks if they were received by recipients"""
        nodeA_key = self.nodeA.getmsgkey()
        nodeC_key = self.nodeC.getmsgkey()

        txn1 = self.nodeC.createmsgtransaction(subject="Message from node C to A",
                                               message="Some content",
                                               public_key=nodeA_key,
                                               threads=4)

        txn2 = self.nodeA.createmsgtransaction(subject="Message from node A to C",
                                               message="Another content",
                                               public_key=nodeC_key,
                                               threads=4)

        txn3 = self.nodeC.createmsgtransaction(subject="Second message from node C to A",
                                               message="Yet another content",
                                               public_key=nodeA_key,
                                               threads=4)

        self.sync_all_till_txns_in_mempool(num_txns=3)
        self.curr_tip = self.nodeA.generate(nblocks=1)[0]
        self.sync_all_till_block(self.curr_tip)

        self.log.info("New block created %s with three msg txns: %s, %s, %s" % (self.curr_tip, txn1, txn2, txn3))
        self.log.info("Current blockchain height is %d" % self.nodeA.getblockcount())

        nodeA_msgs = get_msgs_for_node(self.nodeA)
        nodeA_msgs.sort()

        assert len(nodeA_msgs) == 2
        check_msg_txn(sender_key=nodeC_key,
                      subject="Message from node C to A",
                      content="Some content",
                      msg_str=str(nodeA_msgs[0]))

        check_msg_txn(sender_key=nodeC_key,
                      subject="Second message from node C to A",
                      content="Yet another content",
                      msg_str=str(nodeA_msgs[1]))

        nodeC_msgs = get_msgs_for_node(self.nodeC)
        assert len(nodeC_msgs) == 1
        check_msg_txn(sender_key=nodeA_key,
                      subject="Message from node A to C",
                      content="Another content",
                      msg_str=str(nodeC_msgs[0]))

        nodeB_msgs = get_msgs_for_node(self.nodeB)
        assert len(nodeB_msgs) == 0

        self.log.info("Msg txns received correctly\n")

    def send_correct_block_with_msg_txn(self):
        """Send block with one correct msg txn"""
        tip_height = self.nodeA.getblockcount()
        tip_hash = get_low_32_bits(int(self.nodeA.getbestblockhash(), 16))

        msg_txn = self.mine_msg_txn(tip_height, tip_hash)
        correct_block = self.create_block_with_msgs([msg_txn])
        assert_equal(None, self.nodeA.submitblock(ToHex(correct_block)))

        self.curr_tip = correct_block.hash
        self.correct_msg_txn = msg_txn

        self.sync_all_till_block(self.curr_tip)
        self.log.info("Correct txn %s created in block %s" % (msg_txn.hash, correct_block.hash))
        self.log.info("Current blockchain height is %d\n" % self.nodeA.getblockcount())

    def send_block_with_duplicated_txns(self):
        """Sends block with duplicated msg txns"""
        tip_height = self.nodeA.getblockcount()
        tip_hash = get_low_32_bits(int(self.nodeA.getbestblockhash(), 16))

        msg_txn = self.mine_msg_txn(tip_height, tip_hash)
        msg_txn_copy = copy.deepcopy(msg_txn)

        block_duplicated_msgs = self.create_block_with_msgs([msg_txn, msg_txn_copy])
        self.log.info("Txn %s duplicated in block %s" % (msg_txn.hash, block_duplicated_msgs.hash))

        assert_equal("duplicate-msg-txns-in-block", self.nodeA.submitblock(ToHex(block_duplicated_msgs)))
        self.sync_all_till_block(self.curr_tip)
        self.log.info("Current blockchain height is %d\n" % self.nodeA.getblockcount())

    def send_block_with_msg_transactions_incorrect_tip_height(self):
        """Sends block with msg txns with too high prev block height (prev block not yet in blockchain)
           and too low prev block height (prev block height and hash mismatch)"""
        tip_height = self.nodeA.getblockcount()
        tip_hash = get_low_32_bits(int(self.nodeA.getbestblockhash(), 16))

        msg_txn = self.mine_msg_txn(tip_height+1, tip_hash)
        bad_block = self.create_block_with_msgs([msg_txn])
        self.log.info("Txn %s with too high prev block height in block %s\n" % (msg_txn.hash, bad_block.hash))
        assert_equal("msg-txn-with-bad-prev-block", self.nodeA.submitblock(ToHex(bad_block)))

        msg_txn = self.mine_msg_txn(tip_height-1, tip_hash)
        bad_block = self.create_block_with_msgs([msg_txn])
        self.log.info("Txn %s with too low prev block height in block %s" % (msg_txn.hash, bad_block.hash))

        assert_equal("msg-txn-bad-prev-block-hash", self.nodeA.submitblock(ToHex(bad_block)))
        self.sync_all_till_block(self.curr_tip)

        self.log.info("Current blockchain height is %d\n" % self.nodeA.getblockcount())

    def send_block_with_msg_transactions_with_hash_above_target(self):
        """Sends block with msg txn with hash that is above target"""
        tip_height = self.nodeA.getblockcount()
        tip_hash = get_low_32_bits(int(self.nodeA.getbestblockhash(), 16))

        msg_txn = self.mine_msg_txn_incorrectly(tip_height, tip_hash)
        bad_block = self.create_block_with_msgs([msg_txn])
        self.log.info("Txn %s with too hash above target in block %s" % (msg_txn.hash, bad_block.hash))

        assert_equal("msg-txn-hash-above-target", self.nodeA.submitblock(ToHex(bad_block)))
        self.sync_all_till_block(self.curr_tip)

        self.log.info("Current blockchain height is %d\n" % self.nodeA.getblockcount())

    def send_blocks_with_copied_msg_from_recent_transactions(self):
        """Sends blocks with msg txn copied from older block - txn should be cached as recent txn"""
        copied_msg_txn = self.correct_msg_txn
        assert copied_msg_txn.hash in self.nodeA.getblock(self.curr_tip)["tx"]

        bad_block = self.create_block_with_msgs([copied_msg_txn])
        assert_equal("msg-txn-among-recent", self.nodeB.submitblock(ToHex(bad_block)))
        self.sync_all_till_block(self.curr_tip)
        self.log.info("Copied msg txn %s in block %s" % (copied_msg_txn.hash, bad_block.hash))
        self.log.info("Current blockchain height is %d\n" % self.nodeA.getblockcount())

        self.curr_tip = self.nodes[0].generate(nblocks=5)[4]
        self.sync_all_till_block(self.curr_tip)
        self.log.info("Generated five more blocks")
        self.log.info("Current blockchain height is %d\n" % self.nodeA.getblockcount())

        self.restart_node(3, ["-txindex"])
        connect_nodes_bi(self.nodes, 2, 3)
        self.log.info("Restarted nodeD to check if it loads recent transactions correctly")

        bad_block = self.create_block_with_msgs([copied_msg_txn])
        self.log.info("Copied msg txn %s in block %s\n" % (copied_msg_txn.hash, bad_block.hash))
        assert_equal("msg-txn-among-recent", self.nodeA.submitblock(ToHex(bad_block)))

        bad_block = self.create_block_with_msgs([copied_msg_txn])
        self.log.info("Copied msg txn %s in block %s" % (copied_msg_txn.hash, bad_block.hash))

        assert_equal("msg-txn-among-recent", self.nodeD.submitblock(ToHex(bad_block)))
        self.sync_all_till_block(self.curr_tip)

        self.log.info("Current blockchain height is %d\n" % self.nodeA.getblockcount())

    def send_blocks_with_copied_msg_from_old_transactions(self):
        """Sends blocks with msg txn copied from older block - txn should not be considered too old"""
        self.curr_tip = self.nodes[0].generate(nblocks=1)[0]
        self.sync_all_till_block(self.curr_tip)
        self.log.info("Generated one more block, current blockchain height is %d\n" % self.nodeA.getblockcount())

        copied_msg_txn = self.correct_msg_txn
        assert copied_msg_txn.hash in self.nodeA.getblock(
            self.nodeA.getblockhash(self.nodeA.getblockcount() - MSG_TXN_ACCEPTED_DEPTH))["tx"]

        self.restart_node(3, ["-txindex"])
        connect_nodes_bi(self.nodes, 2, 3)
        self.log.info("Restarted nodeD to check if it loads recent transactions correctly")

        bad_block = self.create_block_with_msgs([copied_msg_txn])
        self.log.info("Copied msg txn %s in block %s\n" % (copied_msg_txn.hash, bad_block.hash))
        assert_equal("msg-txn-too-old", self.nodeA.submitblock(ToHex(bad_block)))

        bad_block = self.create_block_with_msgs([copied_msg_txn])
        self.log.info("Copied msg txn %s in block %s" % (copied_msg_txn.hash, bad_block.hash))
        assert_equal("msg-txn-too-old", self.nodeD.submitblock(ToHex(bad_block)))

        self.log.info("Current blockchain height is %d\n" % self.nodeA.getblockcount())

    def send_block_with_copied_msg_after_reorg(self):
        """Checks handling of msg txns after reorg"""

        self.log.info("Splitting network into two groups: A<->B and C<->D")
        disconnect_nodes(self.nodeB, 2)
        disconnect_nodes(self.nodeC, 1)

        self.log.info("Generate 10 blocks in A<->B network")
        A_B_branch_blocks = list(self.nodeA.generate(nblocks=2))

        # 3rd block
        tip_height = self.nodeA.getblockcount()
        tip_hash = get_low_32_bits(int(self.nodeA.getbestblockhash(), 16))
        msg_txn_1st = self.mine_msg_txn(tip_height, tip_hash)
        block = self.create_block_with_msgs([msg_txn_1st])
        self.nodeA.submitblock(ToHex(block))
        A_B_branch_blocks.append(block.hash)

        # 4th block
        tip_height = self.nodeA.getblockcount()
        tip_hash = get_low_32_bits(int(self.nodeA.getbestblockhash(), 16))
        msg_txn_2nd = self.mine_msg_txn(tip_height, tip_hash)
        block = self.create_block_with_msgs([msg_txn_2nd])
        self.nodeA.submitblock(ToHex(block))
        A_B_branch_blocks.append(block.hash)

        # 5th block
        tip_height = self.nodeA.getblockcount()
        tip_hash = get_low_32_bits(int(self.nodeA.getbestblockhash(), 16))
        msg_txn_3rd = self.mine_msg_txn(tip_height, tip_hash)
        print("msg_txn_2nd", msg_txn_2nd.hash)
        block = self.create_block_with_msgs([msg_txn_3rd])
        # block = self.create_block_with_msgs([])
        self.nodeA.submitblock(ToHex(block))
        A_B_branch_blocks.append(block.hash)

        A_B_branch_blocks.extend(self.nodeA.generate(nblocks=5))
        tip_A_B = A_B_branch_blocks[-1]
        print("tip_A_B", tip_A_B)
        self.sync_group_till_block([self.nodeA, self.nodeB], tip_A_B)
        print("A_B_branch_blocks", A_B_branch_blocks)

        self.log.info("Generate 3 blocks in C<->D network")
        C_D_branch_blocks = list(self.nodeC.generate(nblocks=3))
        tip_C_D = C_D_branch_blocks[-1]
        self.sync_group_till_block([self.nodeC, self.nodeD], tip_C_D)
        assert self.nodeC.getblockcount() != self.nodeB.getblockcount()

        print("Printing tips after reorg\n")
        print("nodeA:", self.nodeA.getblockcount(), self.nodeA.getbestblockhash())
        print("nodeB:", self.nodeB.getblockcount(), self.nodeB.getbestblockhash())
        print("nodeC:", self.nodeC.getblockcount(), self.nodeC.getbestblockhash())
        print("nodeD:", self.nodeD.getblockcount(), self.nodeD.getbestblockhash())

        print("\nC_D_branch_blocks")
        for i, tx in enumerate(C_D_branch_blocks):
            print("%d: %s" % (i, tx_hash(tx)))

        print("\nA_B_branch_blocks")
        for i, tx in enumerate(A_B_branch_blocks):
            print("%d: %s" % (i, tx_hash(tx)))

        self.log.info("Reconnecting networks A<->B and C<->D")
        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_all_till_block(tip_A_B)

        print("Printing current tips\n")
        print("nodeA:", self.nodeA.getblockcount(), self.nodeA.getbestblockhash())
        print("nodeB:", self.nodeB.getblockcount(), self.nodeB.getbestblockhash())
        print("nodeC:", self.nodeC.getblockcount(), self.nodeC.getbestblockhash())
        print("nodeD:", self.nodeD.getblockcount(), self.nodeD.getbestblockhash())

    # def create_empty_block(self, height, prev_hash, block_time):
    #     coinbase = create_coinbase(height)
    #     block = create_block(int(prev_hash, 16),
    #                          coinbase,
    #                          block_time)
    #     block.hashMerkleRoot = block.calc_merkle_root()
    #     block.rehash()
    #     block.solve()
    #
    #     return block

    # def test_blocks_out_of_order(self):
        # time.sleep(5)
        #
        # prev_hash = self.nodeA.getbestblockhash()
        # height = self.nodeA.getblockcount() + 1
        # block_time = self.nodeA.getblock(self.nodeA.getbestblockhash())['time'] + 1
        #
        # block_1 = self.create_empty_block(height, prev_hash, block_time)
        # block_1a = self.create_empty_block(height, prev_hash, block_time+20)
        #
        # prev_hash = block_1.hash
        # height += 1
        # block_time += 1
        # block_2 = self.create_empty_block(height, prev_hash, block_time)
        # block_2a = self.create_empty_block(height, block_1a.hash, block_time + 20)
        #
        # prev_hash = block_2.hash
        # height += 1
        # block_time += 1
        # block_3 = self.create_empty_block(height, prev_hash, block_time)
        # block_3a = self.create_empty_block(height, block_2a.hash, block_time + 20)
        #
        # height += 1
        # block_time += 1
        # block_4a = self.create_empty_block(height, block_3a.hash, block_time + 20)
        #
        # print("block_1:", tx_hash(block_1.hash))
        # print("block_2:", tx_hash(block_2.hash))
        # print("block_3:", tx_hash(block_3.hash))
        # self.nodeA.submitblock(ToHex(block_1))
        # resp = self.nodeA.submitblock(ToHex(block_1a))
        # print("resp", resp)
        # self.nodeA.submitblock(ToHex(block_2))
        # self.nodeA.submitblock(ToHex(block_2a))
        # self.nodeA.submitblock(ToHex(block_3))
        # self.nodeA.submitblock(ToHex(block_3a))
        #
        # curr_tip = self.nodeA.generate(nblocks=1000)[999]
        # print("Curr tip after mining 1000 new blocks:", curr_tip)
        # self.sync_all_till_block(curr_tip)
        #
        # print("Printing current branches 1\n")
        # print("nodeA:", self.nodeA.getchaintips(), "\n")
        # print("nodeB:", self.nodeB.getchaintips(), "\n")
        # print("nodeC:", self.nodeC.getchaintips(), "\n")
        # print("nodeD:", self.nodeD.getchaintips(), "\n")
        #
        # print("block_1a:", tx_hash(block_1a.hash))
        # print("block_2a:", tx_hash(block_2a.hash))
        # print("block_3a:", tx_hash(block_3a.hash))
        # print("block_4a:", tx_hash(block_4a.hash))
        # self.nodeA.submitblock(ToHex(block_4a))
        # time.sleep(10)
        #
        # print("Printing current tips\n")
        # print("nodeA:", self.nodeA.getblockcount(), self.nodeA.getbestblockhash())
        # print("nodeB:", self.nodeB.getblockcount(), self.nodeB.getbestblockhash())
        # print("nodeC:", self.nodeC.getblockcount(), self.nodeC.getbestblockhash())
        # print("nodeD:", self.nodeD.getblockcount(), self.nodeD.getbestblockhash())
        #
        # print("Printing current branches 2\n")
        # print("nodeA:", self.nodeA.getchaintips())
        # print("nodeB:", self.nodeB.getchaintips())
        # print("nodeC:", self.nodeC.getchaintips())
        # print("nodeD:", self.nodeD.getchaintips())

    def run_test(self):
        self.init_test()
        self.test_mining_msg_txns()
        self.send_correct_block_with_msg_txn()
        self.send_block_with_duplicated_txns()
        self.send_block_with_msg_transactions_incorrect_tip_height()
        self.send_block_with_msg_transactions_with_hash_above_target()
        self.send_blocks_with_copied_msg_from_recent_transactions()
        self.send_blocks_with_copied_msg_from_old_transactions()
        self.send_block_with_copied_msg_after_reorg()


if __name__ == '__main__':
    MessengerTest().main()

