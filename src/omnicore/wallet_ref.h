#ifndef WALLET_REF_H
#define WALLET_REF_H

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/rpcwallet.h"
#endif
#include "rpc/server.h"
class CWallet;
extern CWallet* pwalletMain;

#endif
