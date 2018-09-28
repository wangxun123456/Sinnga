#include <util.h>
#include <witness.h>
#include <pubkey.h>
#include <key_io.h>
#include <base58.h>
#include <wallet/wallet.h>
#include <rpc/mining.h>

#define PRODUCE_NODE_COUNT 6


int8_t GetSlotAtTime(int64_t){
    return 0;
}

void NewChainBanner()
{
   std::cerr << "\n"
      "********************************\n"
      "*                              *\n"
      "*   ------- NEW CHAIN ------   *\n"
      "*   -- Welcome to Sinnga! --   *\n"
      "*   ------------------------   *\n"
      "*                              *\n"
      "********************************\n"
      "\n";
   if( GetSlotAtTime(GetTime()) > 200 )
   {
      std::cerr << "Your genesis seems to have an old timestamp\n"
         "Please consider using the --genesis-timestamp option to give your genesis a recent timestamp\n"
         "\n"
         ;
   }
}

enum block_production_condition_enum
{
    produced = 0,
    not_synced = 1,
    not_my_turn = 2,
    not_time_yet = 3,
    no_private_key = 4,
    low_participation = 5,
    lag = 6,
    consecutive = 7,
    exception_producing_block = 8
};



int64_t block_interval=10;//10s
int64_t new_round_begin_time=0;
bool round_generated=false;
uint160 local_address;
std::vector<uint160> witness_keys;

bool GreaterSort(uint160 a,uint160 b){
    return a < b;
}

uint160 Address2uint160(const std::string& address)
{
    const CChainParams& params=Params();
    std::vector<unsigned char> data;
    uint160 hash;
    if (DecodeBase58Check(address, data)) {
        // base58-encoded Bitcoin addresses.
        // Public-key-hash-addresses have version 0 (or 111 testnet).
        // The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
        const std::vector<unsigned char>& pubkey_prefix = params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
        if (data.size() == hash.size() + pubkey_prefix.size() && std::equal(pubkey_prefix.begin(), pubkey_prefix.end(), data.begin())) {
            std::copy(data.begin() + pubkey_prefix.size(), data.end(), hash.begin());
            return hash;
        }
        // Script-hash-addresses have version 5 (or 196 testnet).
        // The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
        const std::vector<unsigned char>& script_prefix = params.Base58Prefix(CChainParams::SCRIPT_ADDRESS);
        if (data.size() == hash.size() + script_prefix.size() && std::equal(script_prefix.begin(), script_prefix.end(), data.begin())) {
            std::copy(data.begin() + script_prefix.size(), data.end(), hash.begin());
            return hash;
        }
    }
    return uint160();
}

bool GetLocalKeyID(CWallet* const pwallet)
{
    bool find=false;
    witness_keys.clear();
    for(auto &address:vWitnessAddresses)
    {
        uint160 u160addr = Address2uint160(address);
        if (u160addr.IsNull()) {
            LogPrintf("Error:address %s is invalid\n",address);
            return false;
        }
        witness_keys.push_back(u160addr);

        CTxDestination dest = DecodeDestination(address);
        auto keyid = GetKeyForDestination(*pwallet, dest);
        if (keyid.IsNull()) {
            continue;
        }
        CKey vchSecret;
        if (!pwallet->GetKey(keyid, vchSecret)) {
            continue;;
        }
        local_address=u160addr;
        find = true;
    }
    std::sort(witness_keys.begin(),witness_keys.end(),GreaterSort);
    return find;
}


//Determine whether the conditions for block production are met and produce blocks
void MaybeProduceBlock(CWallet* const pwallet)
{
    int time_pass=GetTime()-new_round_begin_time;
    int index=time_pass/block_interval;
    if(index>=PRODUCE_NODE_COUNT){
        LogPrintf("index > PRODUCE_NODE_COUNT\n");
        return;
    }
    if(witness_keys[index]==local_address&&!round_generated)
    {
        LogPrintf("Info:Generate block,time pass:%d,index:%d\n",time_pass,index);
        std::shared_ptr<CReserveScript> coinbase_script;
        pwallet->GetScriptForMining(coinbase_script);

        // If the keypool is exhausted, no script is returned at all.  Catch this.
        if (!coinbase_script) {
            LogPrintf("Error: Keypool ran out, please call keypoolrefill first\n");
            throw;
        }

        //throw an error if no script was provided
        if (coinbase_script->reserveScript.empty()) {
            LogPrintf("No coinbase script available\n");
            throw;
        }
        generateBlocks(coinbase_script, 1, 100000, true);
        round_generated=true;
    }
}


void ScheduleProductionLoop()
{
    std::shared_ptr<CWallet> const wallet = GetWallet("");
    CWallet* const pwallet = wallet.get();
    if (!pwallet||!EnsureWalletIsAvailable(pwallet, false))
    {
        LogPrintf("Wallet does not exist or is not loaded\n");
        throw;
    }
    while(!GetLocalKeyID(pwallet))
    {
        LogPrintf("Local witness address not find\n");
        MilliSleep(1000);
        continue;
    }

    try
    {
        while(true)
        {
            while (pwallet->IsLocked())
            {
                LogPrintf("Info: Minting suspended due to locked wallet.\n");;
                MilliSleep(1000);
            }
            if((GetTime()%(PRODUCE_NODE_COUNT*block_interval))<5&&(GetTime()-new_round_begin_time>block_interval)){
                new_round_begin_time=GetTime();
                round_generated=false;
            }
            MaybeProduceBlock(pwallet);
            MilliSleep(1000);
        }
    }
    catch (boost::thread_interrupted)
    {
        LogPrintf("Miner terminated\n");
        throw;
    }
}

// minter thread
void static ThreadMinter(void* parg)
{
    LogPrintf("ThreadMinter started\n");
    try
    {
        ScheduleProductionLoop();
    }
    catch (boost::thread_interrupted) {
        error("minter thread interrupt\n");
    } catch (std::exception& e) {
        error("%s ThreadMinter()", e.what());
    } catch (...) {
        error(NULL, "ThreadMinter()");
    }
    LogPrintf("ThreadMinter exiting\n");
}


// minter
void MintStart(boost::thread_group& threadGroup)
{
    //  mint blocks in the background
    threadGroup.create_thread(boost::bind(&ThreadMinter, nullptr));
}
