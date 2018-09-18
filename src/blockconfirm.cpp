#include "blockconfirm.h"
#include <chainparams.h>
#include <net.h>
#include <validation.h>
#include <consensus/validation.h>
#include <validationinterface.h>

#include <stdio.h>


#define WITNESSCOUNT   3
namespace  {

typedef uint64_t AccountName;
struct PublicKey
{
    uint32_t type;
    std::array<char,33> data;
};
struct ProducerKey
{
    AccountName producerName;
};

struct ProducerSchedule
{
    uint32_t version;
    std::vector<ProducerKey> producers;
};
}

//the block wait confirmed, keep it when you confirm it true
std::shared_ptr<const CBlock> currentBlock;
//first nodeid is connected node, the cblockconfirm node is the origin node
std::set<CBlockConfirm>  setBlockConfirm;
std::map<NodeId,std::set<WitnessId>> mapNodeHaveConfirm;
uint32_t agreedConfirmNum;
uint32_t disagreedConfirmNum;
ProducerSchedule producerSchedule;
uint32_t  witnessCount = WITNESSCOUNT;
//uncofirmed
std::set<uint256> setBadBlock;

static bool IsInSchedule(const CBlockConfirm &confirm)
{
    return true;
}
//is bad or not
static bool IsBadBlockConfirm(const CBlockConfirm &confirm)
{
    return setBadBlock.find(confirm.Hash()) != setBadBlock.end();
}

bool NodeHaveConfirm(const NodeId &id,const WitnessId &witnessId)
{
    const auto &setWitness = mapNodeHaveConfirm.find(id);
    if(setWitness != mapNodeHaveConfirm.end())
    {
        return setWitness->second.find(witnessId) != setWitness->second.end();
    }
    return false;
}
static void AddConfirm(const CBlockConfirm &confirm,const CNode *node = nullptr)
{
    if(node)
    {
        NodeId id = node->GetId();
        WitnessId witnessId = confirm.Id();
        auto setWitness = mapNodeHaveConfirm.find(id);
        if(setWitness != mapNodeHaveConfirm.end())
            mapNodeHaveConfirm[id].insert(witnessId);
        else
            mapNodeHaveConfirm.insert(std::make_pair(id,std::set<WitnessId>{witnessId}));
    }
    if(setBlockConfirm.insert(confirm).second)
    {
        //insert + 1
        agreedConfirmNum += confirm.Confirm();
        disagreedConfirmNum += !confirm.Confirm();
    }
}
static bool HaveConfirm(const CBlockConfirm &confirm)
{
    return  setBlockConfirm.find(confirm) != setBlockConfirm.end();
}
//keep confirm
static bool AcceptConfirm(const std::shared_ptr<const CBlockConfirm> &confirm,const CNode *node)
{
    //get tip
    CBlockIndex *pblock = chainActive.Tip();
    if(pblock == nullptr)
        return false;
    //check empty
    if(node == nullptr)
        return false;
    //is bad block
    if(IsBadBlockConfirm(*confirm))
    {
        return false;
    }
    //is last height
    if(confirm->Height() != pblock->nHeight + 1)
    {
        return false;
    }
    //at Schedule
    if(!IsInSchedule(*confirm))
        return false;
    //already recv
    if(HaveConfirm(*confirm))
    {
        return false;
    }
    AddConfirm(*confirm);
    return true;
}

static bool RelayConfirm(const std::shared_ptr<const CBlockConfirm> &confirm,const CNode *node = nullptr)
{
    //local
    AddConfirm(*confirm,node);

    GetMainSignals().RelayConfirm(confirm);
    //signal slot
    return true;
}
static bool ReleaseConfirm()
{
    currentBlock = std::make_shared<const CBlock>();
    disagreedConfirmNum = 0;
    agreedConfirmNum = 0;
    setBlockConfirm.clear();
    mapNodeHaveConfirm.clear();
    return true;
}

static bool UpdateChain()
{
    if(!currentBlock)
    {
        return false;
    }
    //height()
    int n = witnessCount / 3;
    printf("setBlockConfirm.size = %d\n",setBlockConfirm.size());
    printf("agreedConfirmNum = %d\n",agreedConfirmNum);
    if(agreedConfirmNum > 2*n)
    {
        ReleaseConfirm();
        CValidationState state; // Only used to report errors, not invalidity - ignore it
        if (!ActivateBestChain(state,Params(),currentBlock))
            return error("%s: ActivateBestChain failed (%s)", __func__, FormatStateMessage(state));
    }
    if(disagreedConfirmNum > n)
    {
        setBadBlock.insert(currentBlock->GetHash());
        ReleaseConfirm();
    }
    return true;
}

static bool SetBlock(std::shared_ptr<const CBlock> block)
{
    //already have
    currentBlock = block;
    uint256 blockHash = block->GetHash();
    //check confirm
    for(auto &confirm : setBlockConfirm)
    {
        if(confirm.Hash() != blockHash)
        {
            if(confirm.Confirm())
                agreedConfirmNum--;
            else
                disagreedConfirmNum--;
        }
        setBlockConfirm.erase(confirm);
    }
    UpdateChain();
    return true;
}












bool ProcessConfirm(const std::shared_ptr<const CBlockConfirm> &confirm,const CNode *node)
{
    //check
    if(!AcceptConfirm(confirm,node))
    {
        return false;
    }
    //relay
    if(!RelayConfirm(confirm,node))
        return false;
    //update
    if(!UpdateChain())
        return false;
    return true;
}
bool ProcessNewBlockBft(const CChainParams& chainparams, const std::shared_ptr<const CBlock> pblock,bool fForceProcessing, bool *fNewBlock)
{    
    if(!ProcessNewBlock(chainparams,pblock,fForceProcessing,fNewBlock,true))
    {
        std::shared_ptr<CBlockConfirm> confirm = std::make_shared<CBlockConfirm>(pblock->GetHash(),gArgs.GetArg("-port",200),chainActive.Height()+1,false);
        RelayConfirm(confirm);
        return false;
    }
    //keep block
    SetBlock(pblock);
    //sendconfirm
    std::shared_ptr<CBlockConfirm> confirm = std::make_shared<CBlockConfirm>(pblock->GetHash(),gArgs.GetArg("-port",200),chainActive.Height()+1,true);
    RelayConfirm(confirm);
    //pushmessage
    return UpdateChain();
}
