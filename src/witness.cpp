#include <util.h>
#include <witness.h>
#include <pubkey.h>
#include <key_io.h>
#include <wallet/wallet.h>
#include <rpc/mining.h>

#define PRODUCE_NODE_COUNT 6

int64_t block_interval=10;//10s
int64_t new_round_begin_time=0;
bool round_generated=false;
CKeyID local_keyid;
std::vector<CKeyID> witness_keys;

bool GreaterSort(CKeyID a,CKeyID b){
    return a < b;
}

bool GetLocalKeyID(CWallet* const pwallet)
{
    bool find=false;
    witness_keys.clear();
    for(auto &address:vWitnessAddresses)
    {
        CTxDestination dest = DecodeDestination(address);
        if (!IsValidDestination(dest)) {
            continue;
        }
        auto keyid = GetKeyForDestination(*pwallet, dest);
        if (keyid.IsNull()) {
            continue;
        }
        local_keyid=keyid;
        witness_keys.push_back(keyid);
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
        LogPrintf("index > PRODUCE_NODE_COUNT");
        return;
    }
    if(witness_keys[index]==local_keyid&&!round_generated)
    {
        std::shared_ptr<CReserveScript> coinbase_script;
        pwallet->GetScriptForMining(coinbase_script);

        // If the keypool is exhausted, no script is returned at all.  Catch this.
        if (!coinbase_script) {
            LogPrintf("Error: Keypool ran out, please call keypoolrefill first");
            throw;
        }

        //throw an error if no script was provided
        if (coinbase_script->reserveScript.empty()) {
            LogPrintf("No coinbase script available");
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
        LogPrintf("Wallet does not exist or is not loaded");
        throw;
    }
    while(!GetLocalKeyID(pwallet))
    {
        LogPrintf("Local witness address not find");
        MilliSleep(1000);
        continue;
    }

    try
    {
        while(true)
        {
            while (pwallet->IsLocked())
            {
                LogPrintf("Info: Minting suspended due to locked wallet.");;
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
        LogPrintf("Miner terminated");
        throw;
    }
}

// minter thread
void static ThreadMinter(void* parg)
{
    printf("ThreadMinter started\n");
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
    printf("ThreadMinter exiting\n");
}


// minter
void MintStart(boost::thread_group& threadGroup)
{
    //  mint blocks in the background
    threadGroup.create_thread(boost::bind(&ThreadMinter, nullptr));
}
