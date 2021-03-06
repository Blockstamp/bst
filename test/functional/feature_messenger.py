#!/usr/bin/env python3

"""
Test messenger functionalities (sendmessage, listmsgsinceblock, readmessage, exportmsgkey, importmsgkey, getmsgkey)
"""
import os
from test_framework.test_framework import BitcoinTestFramework
from test_framework.messengertools import get_msgs_for_node, check_msg_txn

class MessengerTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3

    def generate_block(self):
        self.nodeA.generate(nblocks=1)
        self.sync_all()

    def test_sending_msgs(self):
        nodeA_key = self.nodeA.getmsgkey()
        nodeC_key = self.nodeC.getmsgkey()

        self.nodeC.sendmessage(subject="Message from node C to A",
                               message="Some content",
                               public_key=nodeA_key)

        self.nodeA.sendmessage(subject="Message from node A to C",
                               message="Another content",
                               public_key=nodeC_key)

        self.nodeC.sendmessage(subject="Second message from node C to A",
                               message="Yet another content",
                               public_key=nodeA_key)

        self.generate_block()

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

    def test_import_msg_keys(self):
        self.generate_block()
        assert self.nodeA.getmsgkey() != self.nodeB.getmsgkey()

        path = "my_keys"
        self.nodeA.exportmsgkey(destination_path=path)
        self.nodeB.importmsgkey(source_path=path, rescan=True)
        assert self.nodeA.getmsgkey() == self.nodeB.getmsgkey()

        nodeB_msgs = get_msgs_for_node(self.nodeB)
        nodeB_msgs.sort()
        assert len(nodeB_msgs) == 2
        check_msg_txn(sender_key=self.nodeC.getmsgkey(),
                      subject="Message from node C to A",
                      content="Some content",
                      msg_str=str(nodeB_msgs[0]))

        check_msg_txn(sender_key=self.nodeC.getmsgkey(),
                      subject="Second message from node C to A",
                      content="Yet another content",
                      msg_str=str(nodeB_msgs[1]))

        os.remove(path)

    def run_test(self):
        self.nodeA = self.nodes[0]
        self.nodeB = self.nodes[1]
        self.nodeC = self.nodes[2]

        self.test_sending_msgs()
        self.test_import_msg_keys()

if __name__ == '__main__':
    MessengerTest().main()

