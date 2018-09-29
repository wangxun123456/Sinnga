#ifndef OMNICORE_RPCPAYLOAD_H
#define OMNICORE_RPCPAYLOAD_H

#include <univalue.h>

class JSONRPCRequest;

UniValue omni_createpayload_simplesend(const JSONRPCRequest &request);
UniValue omni_createpayload_sendall(const JSONRPCRequest &request);
UniValue omni_createpayload_dexsell(const JSONRPCRequest &request);
UniValue omni_createpayload_dexaccept(const JSONRPCRequest &request);
UniValue omni_createpayload_sto(const JSONRPCRequest &request);
UniValue omni_createpayload_issuancefixed(const JSONRPCRequest &request);
UniValue omni_createpayload_issuancecrowdsale(const JSONRPCRequest &request);
UniValue omni_createpayload_issuancemanaged(const JSONRPCRequest &request);
UniValue omni_createpayload_closecrowdsale(const JSONRPCRequest &request);
UniValue omni_createpayload_grant(const JSONRPCRequest &request);
UniValue omni_createpayload_revoke(const JSONRPCRequest &request);
UniValue omni_createpayload_changeissuer(const JSONRPCRequest &request);
UniValue omni_createpayload_trade(const JSONRPCRequest &request);
UniValue omni_createpayload_canceltradesbyprice(const JSONRPCRequest &request);
UniValue omni_createpayload_canceltradesbypair(const JSONRPCRequest &request);
UniValue omni_createpayload_cancelalltrades(const JSONRPCRequest &request);

#endif // OMNICORE_RPCPAYLOAD_H
