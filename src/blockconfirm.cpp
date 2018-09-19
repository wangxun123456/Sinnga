#include "blockconfirm.h"
#include <chainparams.h>
#include <net.h>
#include <validation.h>
#include <consensus/validation.h>
#include <validationinterface.h>

#include <stdio.h>

/***********************************************************************
 *打印说明：
 * 1.所有的错误信息在全部函数打印
 * 2.其他信息在底层函数打印
 *
 *逻辑说明：
 * 更新主链的两个位置：
 *    1.接收到confirm
 *    2.接收到完整的块
 * 发送给节点更新块的位置：
 *    1.开始同步的时候
 *    2.接收到新块，且父块不是创世块，此时发送压缩块
 *    3.主链已经更新
 * activechain更新的条件：
 *    1.havedata
 *    2.not bad
 *    3.confirmed
 * activechain更新后需要做的事情：
 *    1.清除mapblockconfirm中小于等于它高度的
 *    2.清除mapUnconfirmBlock中小于等于它高度的
 *    3.清除mapNodeHaveConfirm中比他小的
 *
 *
 * confirm信息判定：
 *    1.一个见证节点对一个高度只能发送一个confirm，发送多个则判定该节点的判定无效
 *
 * 特殊情况：
 *    1.收到下一个块但是上一个块没有验证成功
 *       等待这一个块的验证，如果这一个块验证成功，则上个也成功，否者等待下一个块
 *    2.收到足够的confirm信息，但是没有收到header
 *       向节点发送请求得到block
 *    3.收到足够的confirm信息，没有得到comfirm信息
 *       向节点发送请求得到block
 *
 *
 ***********************************************************************/

//所有的见证者的个数
#define MAX_WITNESS_COUNT          1
//收到的confirm信息最早提前的时间
#define MAX_CONFIRM_SEC_FRONT      3
//创建块等待CONFIRM的最长时间(秒)
#define MAX_CONFIRM_SEC_WAIT       3
//接受比当前块高度高多少的高度
#define MAX_CONFIRM_HEIGHT_BACK    10

/********************************关于confirm的数据结构*******************/
namespace
{
//收到的confirm信息,只保存一个时间段的confirm信息
std::map<uint256,std::set<CBlockConfirm>> mapBlockConfirm;
//节点拥有的confirm信息
std::map<NodeId,std::set<CBlockConfirm>> mapNodeHaveConfirm;

//见证节点的最大数目
uint32_t  witnessCount = MAX_WITNESS_COUNT;
//见证节点confirm的最大高度
std::map<std::string,uint32_t> mapWitnessMaxConfirmHeight;
}
/***************************************查询*************************************/

/*************************************************
 * 函数说明：
 * 1.index标志为BLOCK_VALID_TRANSACTIONS
 * 2.index标志为BFT有效
 ************************************************/
static bool IsBftValid(const uint256 &blockHash)
{
    CBlockIndex *pindex = LookupBlockIndex(blockHash);
    if(pindex)
        return (pindex->nStatus & BLOCK_VALID_TRANSACTIONS) && (pindex->nStatus & BLOCK_VALID_CONFIRM);
    return false;
}
static bool IsBftValid(const CBlockConfirm &confirm)
{
    return IsBftValid(confirm.Hash());
}

/*************************************************
 * 函数说明：
 * 1.检验发送者是否为见证者
 * 2.检验该见证节点是否发送过比他高的confirm
 ************************************************/
static inline bool CheckWitness(const CBlockConfirm &confirm)
{
    //TODO 对签名的校验
    const auto & witnessHeight = mapWitnessMaxConfirmHeight.find(confirm.Id());
    if(witnessHeight != mapWitnessMaxConfirmHeight.end())
        if(witnessHeight->second >= confirm.Height())
            return false;
    return true;
}
/*************************************************
 * 函数说明：
 * 1.查询mapBlockConfirm是否存在该confirm
 ************************************************/
static bool HaveConfirm(const CBlockConfirm &confirm)
{
    const auto &setConfirm = mapBlockConfirm.find(confirm.Hash());
    if(setConfirm == mapBlockConfirm.end())
        return false;
    if(setConfirm->second.find(confirm) == setConfirm->second.end())
        return false;
    return true;
}
/***************************************************
 * 函数说明：
 * 1.检验发送者是否为见证者
 * 2.检验块的高度是否合适
 * 3.检验发送着是否已经发送过该块的confirm
 * 4.检验发送者是否发送过该高度的confirm
 **************************************************/
static bool CheckConfirm(const CBlockConfirm &confirm)
{
    if(!CheckWitness(confirm))
        return error("%s: CheckWitness failed ", __func__);
    if(confirm.Height() <= chainActive.Tip()->nHeight)
        return error("%s: Height too low(%s)", __func__,confirm.ToString());
    if(confirm.Height() > chainActive.Tip()->nHeight + MAX_CONFIRM_HEIGHT_BACK)
        if(!LookupBlockIndex(confirm.Hash()))
            return error("%s: Height too high and not have block(%s)", __func__,confirm.ToString());
    if(!HaveConfirm(confirm))
        return error("%s: Already haveConfirm(%s)", __func__,confirm.ToString());
    return true;
}

/***************************************************
 * 函数说明：
 * 1.从index中获取hash的高度
 * 2.从mapBlockConfirm中获取块的高度
 **************************************************/
static int GetHeight(const uint256& blockHash)
{
    CBlockIndex *pindex = LookupBlockIndex(blockHash);
    if(pindex)
        return pindex->nHeight;
    const auto & setConfirm = mapBlockConfirm.find(blockHash);
    if(setConfirm != mapBlockConfirm.end())
    {
        std::map<int,uint32_t> mcount;
        for(const auto & confirm : setConfirm->second)
        {
            if(mcount.find(confirm.Height()) != mcount.end())
                ++mcount[confirm.Height()];
            else
                mcount[confirm.Height()] = 1;
        }
        int height= -1; uint32_t count = 0;
        for(const auto & p : mcount)
        {
            if(p.second > count)
            {
                count = p.second;
                height = p.second;
            }
        }
        return height;
    }
    return -1;
}
/***************************************操作*************************************/
/***************************************************
 * 函数说明：
 * 1.生成本地的confirm信息
 **************************************************/
static std::shared_ptr<const CBlockConfirm> GenerateConfirm(const std::shared_ptr<const CBlock> block)
{
    std::string witnessId;
    witnessId += gArgs.GetArg("-ip","") + ":" + boost::lexical_cast<std::string>(gArgs.GetArg("-port",200));
    CBlockIndex *pindex = LookupBlockIndex(block->GetHash());
    return std::make_shared<CBlockConfirm>(CBlockConfirm(block->GetHash(),witnessId,pindex?pindex->nHeight:-1));
}
/***************************************************
 * 函数说明：
 * 1.将confirm添加到节点信息中
 **************************************************/
static void AddConfirmToLocal(const CBlockConfirm &confirm)
{
    mapBlockConfirm[confirm.Hash()].emplace(confirm);
}

/***************************************************
 * 函数说明：
 * 1.将confirm添加到本地已接受的confirm信息中
 **************************************************/
static void AddConfirmToNode(const CBlockConfirm &confirm,const CNode *node)
{
    if(node == nullptr)
        return;
    mapNodeHaveConfirm[node->GetId()].emplace(confirm);
}

/***************************************************
 * 函数说明：
 * 1.将confirm添加到本地已接受的confirm信息中
 **************************************************/
static void AddConfirmToWitness(const CBlockConfirm &confirm)
{
    if(mapWitnessMaxConfirmHeight.find(confirm.Id()) == mapWitnessMaxConfirmHeight.end())
    {
        mapWitnessMaxConfirmHeight[confirm.Id()] = confirm.Height();
        return;
    }
    mapWitnessMaxConfirmHeight[confirm.Id()]  = std::max<int>(mapWitnessMaxConfirmHeight[confirm.Id()],confirm.Height());
}
/***************************************************
 * 函数说明：
 * 1.查看是否有足够的confirm
 **************************************************/
static bool HaveEnoughConfirm(const uint256 &blockHash)
{
    return mapBlockConfirm.count(blockHash) > 2*witnessCount/3;
}
/***************************************************
 * 函数说明：
 * 1.检验是否满足bft要求
 **************************************************/
static void TrySetConfirmFlag(const CBlockConfirm &confirm)
{
    if(HaveEnoughConfirm(confirm.Hash()))
    {
        CBlockIndex *pindex = LookupBlockIndex(confirm.Hash());
        if(pindex)
        {
            if(pindex->nStatus & BLOCK_VALID_TRANSACTIONS)
                pindex->nStatus |= BLOCK_VALID_CONFIRM;
        }
    }
}
/***************************************************
 * 函数说明：
 * 1.将confirm添加到节点信息中
 * 2.将confirm添加到本地已接受的confirm信息中
 * 3.将confirm的高度添加到witness最大高度中
 * 4.如果confirm的信息满足bft要求，则将对应的block信息设置标志
 **************************************************/
static void AddConfirm(const CBlockConfirm &confirm,const CNode *node = nullptr)
{
    AddConfirmToLocal(confirm);
    AddConfirmToNode(confirm,node);
    AddConfirmToWitness(confirm);
    TrySetConfirmFlag(confirm);
}

/***************************************************
 * 函数说明：
 * 1.将confirm发送至连接节点
 **************************************************/
static void RelayConfirm(const std::shared_ptr<const CBlockConfirm> &confirm)
{
    GetMainSignals().RelayConfirm(confirm);
}

/***************************************************
 * 函数说明：
 * 1.释放对应块的confirm信息
 * 2.释放节点的对应信息
 **************************************************/
static bool ReleaseConfirm()
{
    int height = chainActive.Tip()->nHeight;
    for(const auto & pconfirm : mapBlockConfirm)
    {
        if(GetHeight(pconfirm.first) == height)
            mapBlockConfirm.erase(pconfirm.first);
    }

    return true;
}

/***************************************************
 * 函数说明：
 * 1.检查confirm是否合适
 * 2.保存confirm信息
 **************************************************/
static bool AcceptConfirm(const std::shared_ptr<const CBlockConfirm> &confirm,const CNode *node)
{
    if(CheckConfirm(*confirm))
        return false;
    AddConfirm(*confirm,node);
    return true;
}

/***************************************************
 * 函数说明：
 * 1.更新主链
 **************************************************/
static bool ActivateBestChainBft(const CBlockConfirm &confirm)
{
    if(IsBftValid(confirm))
    {
        HLOG("in active best chain");
        CValidationState state; // Only used to report errors, not invalidity - ignore it
        if (!ActivateBestChain(state,Params()))
            return error("%s: ActivateBestChain failed (%s)", __func__, FormatStateMessage(state));
        ReleaseConfirm();
    }
    return true;
}

/*************************************************外部可以调用的函数************************************/

/*************************************************
 * 函数说明：
 * 1.查看该节点是否已经存在confirm
 ************************************************/
bool NodeHaveConfirm(const NodeId &id,const CBlockConfirm &confirm)
{
    const auto & setConfirm = mapNodeHaveConfirm.find(id);
    if(setConfirm == mapNodeHaveConfirm.end())
        return false;
    if(setConfirm->second.find(confirm) == setConfirm->second.end())
        return false;
    return true;
}
/***************************************************
 * 函数调用位置：
 *    1.接收到confirm信息后进行的操作
 * 函数操作：
 *    1.接受confirm
 *    2.传递confirm
 *    3.更新主链
 **************************************************/
bool ProcessConfirm(const std::shared_ptr<const CBlockConfirm> &confirm,const CNode *node)
{
    if(!AcceptConfirm(confirm,node))
        return false;
    RelayConfirm(confirm);
    return ActivateBestChainBft(*confirm);
}
/***************************************************
 * 函数调用位置：
 *    1.通过压缩块的形式接收到完成
 *    2.创建初始块
 * 函数操作：
 *    1.校验新块（为了避免改动，直接调用processnewblock）
 *    2.保存块（可以不用）
 *    3.发送confirm信息
 *    4.更新主链
 **************************************************/
bool ProcessNewBlockBft(const CChainParams& chainparams, const std::shared_ptr<const CBlock> pblock,bool fForceProcessing, bool *fNewBlock)
{    
    if(!ProcessNewBlock(chainparams,pblock,fForceProcessing,fNewBlock,true))
        return false;
    const auto & confirm = GenerateConfirm(pblock);
    AddConfirm(*confirm);
    RelayConfirm(confirm);
    return ActivateBestChainBft(*confirm);
}
