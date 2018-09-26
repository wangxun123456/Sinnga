#include "blockconfirm.h"
#include <chainparams.h>
#include <net.h>
#include <validation.h>
#include <consensus/validation.h>
#include <validationinterface.h>

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <boost/thread/lock_factories.hpp>


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
 *    3.清除mapConfirmNodeHave中比他小的
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
#define MAX_WITNESS_COUNT          6
//收到的confirm信息最早提前的时间
#define MAX_CONFIRM_SEC_FRONT      3
//创建块等待CONFIRM的最长时间(秒)
#define MAX_CONFIRM_SEC_WAIT       3
//接受比当前块高度高多少的高度
#define MAX_CONFIRM_HEIGHT_BACK    10
//保留历史记录的个数
#define MAX_RECORD_KEEPING_NUM     10

/********************************关于confirm的数据结构*******************/



int get_local_ip(const char *eth_inf, char *ip)
{
    int sd;
    struct sockaddr_in sin;
    struct ifreq ifr;

    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == sd)
    {
        printf("socket error: %s\n", strerror(errno));
        return -1;
    }

    strncpy(ifr.ifr_name, eth_inf, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = 0;

    // if error: No such device
    if (ioctl(sd, SIOCGIFADDR, &ifr) < 0)
    {
        printf("ioctl error: %s\n", strerror(errno));
        close(sd);
        return -1;
    }

    memcpy(&sin, &ifr.ifr_addr, sizeof(sin));
    snprintf(ip, 50, "%s", inet_ntoa(sin.sin_addr));

    close(sd);
    return 0;
}


namespace
{
char localIp[50];
//收到的confirm信息,只保存一个时间段的confirm信息
std::map<uint256,std::set<CBlockConfirm>> mapBlockConfirm;
//收到的confirm信息，保存没有收到块的confirm
std::map<uint256,std::set<CBlockConfirm>> mapConfirmNotHaveBlock;
//节点拥有的confirm信息
std::map<CBlockConfirm,std::set<NodeId>> mapConfirmNodeHave;
//见证节点confirm的最大高度
std::map<std::string,uint32_t> mapWitnessMaxConfirmHeight;
//见证节点的最大数目
uint32_t  witnessCount = MAX_WITNESS_COUNT;

boost::mutex mapBlockConfirmLock;
boost::mutex mapConfirmNotHaveBlockLock;
boost::mutex mapConfirmNodeHaveLock;
boost::mutex mapWitnessMaxConfirmHeightLock;

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
static bool CheckWitness(const CBlockConfirm &confirm)
{
    //TODO 对签名的校验
    HLOG("in");
    {
        make_unique_lock(mapWitnessMaxConfirmHeightLock);
        const auto & witnessHeight = mapWitnessMaxConfirmHeight.find(confirm.Id());
        if(witnessHeight != mapWitnessMaxConfirmHeight.end())
        {
            if(witnessHeight->second > confirm.Height())
                return false;
        }
    }
    HLOG("out");

    return true;
}


/*************************************************
 * 函数说明：
 * 1.查询mapBlockConfirm是否存在该confirm
 * 2.查询mapconfirmnothaveblock是否存在confirm
 ************************************************/
static bool HaveConfirm(const CBlockConfirm &confirm)
{
    HLOG("in");
    {
        make_unique_lock(mapBlockConfirmLock);
        const auto &setConfirm = mapBlockConfirm.find(confirm.Hash());
        if(setConfirm == mapBlockConfirm.end())
            return false;
        if(setConfirm->second.find(confirm) == setConfirm->second.end())
            return false;
    }
    {
        make_unique_lock(mapConfirmNotHaveBlockLock);
        const auto &setConfirm = mapConfirmNotHaveBlock.find(confirm.Hash());
        if(setConfirm == mapConfirmNotHaveBlock.end())
            return false;
        if(setConfirm->second.find(confirm) == setConfirm->second.end())
            return false;
    }
    HLOG("out");

    return true;
}

/***************************************************
 * 函数说明：
 * 1.从index中获取hash的高度
 * 2.从mapBlockConfirm中获取块的高度
 **************************************************/
static int GetHeight(const uint256& blockHash)
{

    HLOG("in");

    CBlockIndex *pindex = LookupBlockIndex(blockHash);
    if(pindex)
        return pindex->nHeight;
    make_unique_lock(mapBlockConfirmLock);
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
        HLOG("mcount size = %d",mcount.size());

        int height= -1; uint32_t count = 0;
        for(const auto & p : mcount)
        {
            if(p.second > count)
            {
                count = p.second;
                height = p.second;
            }
        }
        HLOG("height = %d",height);

        return height;
    }
    HLOG("out");

    return -1;
}
/***************************************操作*************************************/
/***************************************************
 * 函数说明：
 * 1.生成本地的confirm信息
 **************************************************/
static std::shared_ptr<const CBlockConfirm> GenerateConfirm(const std::shared_ptr<const CBlock> block)
{
    HLOG("in");
    std::string witnessId;
    if(!strlen(localIp))
        get_local_ip("ens33",localIp);
    witnessId += localIp + std::string(":") + boost::lexical_cast<std::string>(gArgs.GetArg("-port",200));
    CBlockIndex *pindex = LookupBlockIndex(block->GetHash());
    return std::make_shared<CBlockConfirm>(CBlockConfirm(block->GetHash(),witnessId,pindex?pindex->nHeight:-1));
    HLOG("out");
}
/***************************************************
 * 函数说明：
 * 1.将confirm添加到节点信息中
 **************************************************/
static void AddConfirmToLocal(const CBlockConfirm &confirm)
{
    HLOG("in");

    if(!LookupBlockIndex(confirm.Hash()))
    {
        make_unique_lock(mapConfirmNotHaveBlockLock);
        mapConfirmNotHaveBlock[confirm.Hash()].insert(confirm);
    }
    else
    {
        make_unique_lock(mapBlockConfirmLock);
        mapBlockConfirm[confirm.Hash()].insert(confirm);
    }
    HLOG("out");

}

/***************************************************
 * 函数说明：
 * 1.将confirm添加到本地已接受的confirm信息中
 **************************************************/
void AddConfirmToNode(const CBlockConfirm &confirm,const CNode *node)
{
    HLOG("in");
    if(node == nullptr)
        return;
    {
        make_unique_lock(mapConfirmNodeHaveLock);
        const auto & pconfirm = mapConfirmNodeHave.find(confirm);
        if(pconfirm == mapConfirmNodeHave.end())
            mapConfirmNodeHave.insert(std::pair<CBlockConfirm,std::set<NodeId>>(confirm,{node->GetId()}));
        else
            mapConfirmNodeHave[confirm].insert(node->GetId());
    }

    HLOG("out");

}

/***************************************************
 * 函数说明：
 * 1.将confirm添加到本地已接受的confirm信息中
 **************************************************/
static void AddConfirmToWitness(const CBlockConfirm &confirm)
{
    HLOG("in");
    make_unique_lock(mapWitnessMaxConfirmHeightLock);

    if(mapWitnessMaxConfirmHeight.find(confirm.Id()) == mapWitnessMaxConfirmHeight.end())
    {
        mapWitnessMaxConfirmHeight[confirm.Id()] = confirm.Height();
        return;
    }
    mapWitnessMaxConfirmHeight[confirm.Id()]  = std::max<int>(mapWitnessMaxConfirmHeight[confirm.Id()],confirm.Height());
    HLOG("out");

}

/***************************************************
 * 函数说明：
 * 1.查看是否有足够的confirm
 **************************************************/
static bool HaveEnoughConfirm(const uint256 &blockHash)
{
    HLOG("in");
    make_unique_lock(mapBlockConfirmLock);
    const auto & pcon= mapBlockConfirm.find(blockHash);
    if(pcon == mapBlockConfirm.end())
        return false;
    return pcon->second.size() > 2*witnessCount/3;
    //    return pcon->second.size() > 2;
    HLOG("out");

}

/***************************************************
 * 函数说明：
 * 1.检验是否满足bft要求
 **************************************************/
static void TrySetConfirmFlag(const CBlockConfirm &confirm)
{
    HLOG("in");

    if(HaveEnoughConfirm(confirm.Hash()))
    {
        CBlockIndex *pindex = LookupBlockIndex(confirm.Hash());
        if(pindex)
        {
            if(pindex->nStatus & BLOCK_VALID_TRANSACTIONS)
            {
                pindex->nStatus |= BLOCK_VALID_CONFIRM;
            }
        }
    }
    HLOG("out");
}

/***************************************************
 * 函数说明：
 * 1.查看mapconfirmnothaveblock是否存在它的confirm
 * 2.移动对应的confirm
 **************************************************/
static void UpdateConfirmToBlock(const std::shared_ptr<const CBlock> &pblock)
{
    HLOG("in");

    uint256 blockHash = pblock->GetHash();
    if(mapConfirmNotHaveBlock.find(blockHash) != mapConfirmNotHaveBlock.end())
    {
        make_unique_lock(mapBlockConfirmLock,boost::adopt_lock);
        make_unique_lock(mapConfirmNotHaveBlockLock,boost::adopt_lock);
        lock(mapBlockConfirmLock,mapConfirmNotHaveBlockLock);
        mapBlockConfirm[blockHash].insert(mapConfirmNotHaveBlock[blockHash].begin(),mapConfirmNotHaveBlock[blockHash].end());
        mapConfirmNotHaveBlock.erase(blockHash);
    }
    HLOG("out");

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
    HLOG("in");
    AddConfirmToLocal(confirm);
    AddConfirmToNode(confirm,node);
    AddConfirmToWitness(confirm);
    TrySetConfirmFlag(confirm);
    HLOG("out");
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
 * 1.检验发送者是否为见证者
 * 2.检验块的高度是否合适
 * 3.检验发送着是否已经发送过该块的confirm
 * 4.检验发送者是否发送过该高度的confirm
 * 5.检验高度和index的高度不一致
 **************************************************/
static bool CheckConfirm(const std::shared_ptr<const CBlockConfirm> &confirm)
{
    HLOG("in");

    CBlockIndex *pindex = LookupBlockIndex(confirm->Hash());
    if(!CheckWitness(*confirm))
    {
        HLOG("checkwitness error");
        return error("%s: CheckWitness failed ", __func__);
    }
    HLOG("check height");
    if(confirm->Height() <= chainActive.Tip()->nHeight)
    {
        if(confirm->Height() > chainActive.Tip()->nHeight)
            RelayConfirm(confirm);
        return error("%s: Height too low(%s)", __func__,confirm->ToString());
    }
    if(confirm->Height() > chainActive.Tip()->nHeight + MAX_CONFIRM_HEIGHT_BACK)
    {
        if(!pindex)
        {
            RelayConfirm(confirm);
            return error("%s: Height too high and not have block(%s)", __func__,confirm->ToString());
        }
    }

    if(HaveConfirm(*confirm))
    {
        RelayConfirm(confirm);
        return error("%s: Already haveConfirm(%s)", __func__,confirm->ToString());
    }

    if(pindex && pindex->nHeight != confirm->Height())
        return error("%s:Confirm height is wrong(%s)",__func__,confirm->ToString());

    HLOG("out");

    return true;
}

/***************************************************
 * 函数说明：
 * 1.释放对应块的confirm信息
 * 2.释放节点的对应信息
 * 3.释放没有块的confirm
 **************************************************/
static void ReleaseConfirm()
{
    HLOG("in");
    int height = chainActive.Tip()->nHeight-MAX_RECORD_KEEPING_NUM;
    {
        make_unique_lock(mapBlockConfirmLock);
        for( auto iter = mapBlockConfirm.begin();iter != mapBlockConfirm.end();)
        {
            if(GetHeight(iter->first) <= height)
            {
                HLOG("before erase mapBlockConfirm");
                HLOG("mapBlockConfirm.size = %d",mapBlockConfirm.size());
                mapBlockConfirm.erase(iter++);
                HLOG("after mapBlockConfirm.size = %d",mapBlockConfirm.size());

                HLOG("after erase mapBlockConfirm");

            }
            else
            {
                iter++;
            }
        }
    }
    {
        make_unique_lock(mapConfirmNodeHaveLock);
        for(auto iter = mapConfirmNodeHave.begin();iter != mapConfirmNodeHave.end();)
        {
            if(GetHeight(iter->first.Hash()) <= height)
            {
                HLOG("GetHeight(iter->first.Hash() = %d height = %d\n",GetHeight(iter->first.Hash()),height);
                HLOG("before erase mapConfirmNodeHave");
                HLOG("mapConfirmNodeHave.size = %d",mapConfirmNodeHave.size());
                mapConfirmNodeHave.erase(iter++);
                HLOG("after mapConfirmNodeHave.size = %d",mapConfirmNodeHave.size());
                HLOG("after erase mapConfirmNodeHave");
            }
            else
            {
                iter++;
            }
        }
    }

    {
        make_unique_lock(mapConfirmNotHaveBlockLock);
        for(auto iter = mapConfirmNotHaveBlock.begin();iter != mapConfirmNotHaveBlock.end();)
        {

            if(GetHeight(iter->first) <= height)
            {
                HLOG("before erase mapConfirmNotHaveBlock");
                HLOG("mapConfirmNotHaveBlock.size = %d",mapConfirmNotHaveBlock.size());
                mapConfirmNotHaveBlock.erase(iter++);
                HLOG("after mapConfirmNotHaveBlock.size = %d",mapConfirmNotHaveBlock.size());

                HLOG("before erase mapConfirmNotHaveBlock");

            }
            else
            {
                iter++;
            }
        }
    }
    HLOG("out");

}

/***************************************************
 * 函数说明：
 * 1.检查confirm是否合适
 * 2.保存confirm信息
 **************************************************/
static bool AcceptConfirm(const std::shared_ptr<const CBlockConfirm> &confirm,const CNode *node)
{

    HLOG("in");

    HLOG("getconfirm from %s\n",confirm->Id().c_str());
    if(!CheckConfirm(confirm))
        return false;
    AddConfirm(*confirm,node);
    HLOG("out");
    return true;

}

/***************************************************
 * 函数说明：
 * 1.更新主链
 **************************************************/
static bool ActivateBestChainBft(const CBlockConfirm &confirm)
{
    HLOG("in");
    if(IsBftValid(confirm))
    {
        HLOG("active");
        CValidationState state; // Only used to report errors, not invalidity - ignore it
        if (!ActivateBestChain(state,Params()))
            return error("%s: ActivateBestChain failed (%s)", __func__, FormatStateMessage(state));
        ReleaseConfirm();
    }
    HLOG("out");

    return true;
}

/*************************************************外部可以调用的函数************************************/

/*************************************************
 * 函数说明：
 * 1.查看该节点是否已经存在confirm
 ************************************************/
bool NodeHaveConfirm(const NodeId &id,const CBlockConfirm &confirm)
{
    HLOG("in");

    {
        make_unique_lock(mapConfirmNodeHaveLock);
        const auto & setNode = mapConfirmNodeHave.find(confirm);
        if(setNode == mapConfirmNodeHave.end())
            return false;
        if(setNode->second.find(id) == setNode->second.end())
            return false;
    }
    HLOG("out");

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
    HLOG("in");

    try
    {
        if(!AcceptConfirm(confirm,node))
            return false;
        RelayConfirm(confirm);
        HLOG("out");

        ActivateBestChainBft(*confirm);
    }
    catch(...)
    {
        HLOG("ProcessConfirm catch.......................");
        throw;
    }
    return true;
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
    try
    {
        HLOG("in");
        if(!ProcessNewBlock(chainparams,pblock,fForceProcessing,fNewBlock,true))
            return false;
        HLOG("pblock height = %d",LookupBlockIndex(pblock->GetHash())->nHeight);
        const auto & confirm = GenerateConfirm(pblock);
        AddConfirm(*confirm);
        RelayConfirm(confirm);
        UpdateConfirmToBlock(pblock);
        HLOG("out");
        ActivateBestChainBft(*confirm);
    }
    catch(...)
    {
        HLOG("ProcessNewBlockBft catching");
        throw;
    }
    return true;
}
