1.The test case rpc_rawtransaction.py contains the following RPCs:
   - createrawtransaction
   - signrawtransactionwithwallet
   - sendrawtransaction
   - decoderawtransaction
   - getrawtransaction

2.Before debugging rpc_rawtransaction.py file, we need change the rpc_timewait to 60 * 120 (two hous) in order to avoiding the RPC call failure resulted by timeout during debugging. 
    
    class RawTransactionsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [["-addresstype=legacy"], ["-addresstype=legacy"], ["-addresstype=legacy"]]
        
        #set rpc_timewait to 900 for debugging c++ code 
        self.rpc_timewait = 60 * 120

3.After running rpc_rawtransaction.py, Bitcoin test framework will start three bitcoind nodes with different parameter datadir as below,

    12476 pts/2    SLl+   0:28 /home/hunter/bitcoin/src/bitcoind -datadir=/tmp/test4o6ctbl2/node0 -logtimemicros -debug -debugexclude=libevent -debugexclude=leveldb -mocktime=0 -uacomment=testnode0 -addresstype=legacy
    12477 pts/2    SLl+   1:18 /home/hunter/bitcoin/src/bitcoind -datadir=/tmp/test4o6ctbl2/node1 -logtimemicros -debug -debugexclude=libevent -debugexclude=leveldb -mocktime=0 -uacomment=testnode1 -addresstype=legacy
    12478 pts/2    SLl+   1:18 /home/hunter/bitcoin/src/bitcoind -datadir=/tmp/test4o6ctbl2/node2 -logtimemicros -debug -debugexclude=libevent -debugexclude=leveldb -mocktime=0 -uacomment=testnode2 -addresstype=legacy
  
  that makes us to be easy to distinguish which pid which bitciond node has, when test case call RPC createrawtransaction on node 0, we need attach to pid 12476 for debugging c++ source code.

4.Check that createrawtransaction accepts an array and object as outputs
        
        self.log.info('Check that createrawtransaction accepts an array and object as outputs')
        tx = CTransaction()
        # One output
        tx.deserialize(BytesIO(hex_str_to_bytes(self.nodes[2].createrawtransaction(inputs=[{'txid': txid, 'vout': 9}], outputs={address: 99}))))
        assert_equal(len(tx.vout), 1)
        assert_equal(
            bytes_to_hex_str(tx.serialize()),
            self.nodes[2].createrawtransaction(inputs=[{'txid': txid, 'vout': 9}], outputs=[{address: 99}]),
        )
        # Two outputs
        tx.deserialize(BytesIO(hex_str_to_bytes(self.nodes[2].createrawtransaction(inputs=[{'txid': txid, 'vout': 9}], outputs=OrderedDict([(address, 99), (address2, 99)])))))
        assert_equal(len(tx.vout), 2)
        assert_equal(
            bytes_to_hex_str(tx.serialize()),
            self.nodes[2].createrawtransaction(inputs=[{'txid': txid, 'vout': 9}], outputs=[{address: 99}, {address2: 99}]),
        )
        # Two data outputs
        tx.deserialize(BytesIO(hex_str_to_bytes(self.nodes[2].createrawtransaction(inputs=[{'txid': txid, 'vout': 9}], outputs=multidict([('data', '99'), ('data', '99')])))))
        assert_equal(len(tx.vout), 2)
        assert_equal(
            bytes_to_hex_str(tx.serialize()),
            self.nodes[2].createrawtransaction(inputs=[{'txid': txid, 'vout': 9}], outputs=[{'data': '99'}, {'data': '99'}]),
        )
        # Multiple mixed outputs
        tx.deserialize(BytesIO(hex_str_to_bytes(self.nodes[2].createrawtransaction(inputs=[{'txid': txid, 'vout': 9}], outputs=multidict([(address, 99), ('data', '99'), ('data', '99')])))))
        assert_equal(len(tx.vout), 3)
        assert_equal(
            bytes_to_hex_str(tx.serialize()),
            self.nodes[2].createrawtransaction(inputs=[{'txid': txid, 'vout': 9}], outputs=[{address: 99}, {'data': '99'}, {'data': '99'}]),
        )