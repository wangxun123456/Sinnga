#ifndef OMNICORE_RPCRAWTX_H
#define OMNICORE_RPCRAWTX_H

#include <univalue.h>

class JSONRPCRequest;

UniValue omni_decodetransaction(const JSONRPCRequest &request);
UniValue omni_createrawtx_opreturn(const JSONRPCRequest &request);
UniValue omni_createrawtx_multisig(const JSONRPCRequest &request);
UniValue omni_createrawtx_input(const JSONRPCRequest &request);
UniValue omni_createrawtx_reference(const JSONRPCRequest &request);
UniValue omni_createrawtx_change(const JSONRPCRequest &request);

#endif // OMNICORE_RPCRAWTX_H
