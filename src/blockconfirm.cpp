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

//所有的见证者的个数
#define MAX_WITNESS_COUNT          6
//收到的confirm信息最早提前的时间
#define MAX_CONFIRM_SEC_FRONT      3
//创建块等待CONFIRM的最长时间(秒)
#define MAX_CONFIRM_SEC_WAIT       3
//接受比当前块高度高多少的高度
#define MAX_CONFIRM_HEIGHT_BACK    10


char localIp[50];
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

class CNodeConfirm
{
public:
    typedef std::map<uint256,std::set<CBlockConfirm>> ConfirmMap;

    CNodeConfirm(){}
    CNodeConfirm(const CBlockConfirm &confirm){m_mapConfirms.emplace(std::pair<int,ConfirmMap>{confirm.Height(),{{confirm.Hash(),std::set<CBlockConfirm>{confirm}}}});}

    bool InsertConfirm(const CBlockConfirm &confirm)
    {
        if(HaveConfirm(confirm))
            return false;
        return m_mapConfirms[confirm.Height()][confirm.Hash()].emplace(confirm).second;
    }

    bool HaveConfirm(const CBlockConfirm &confirm)
    {
        auto hConfirms = m_mapConfirms.find(confirm.Height());
        if(hConfirms != m_mapConfirms.end())
        {
            auto confirms = hConfirms->second.find(confirm.Hash());
            if(confirms != hConfirms->second.end())
            {
                return confirms->second.find(confirm) != confirms->second.end();
            }
        }
        return false;
    }

    void RemoveConfirms(int height)
    {
        m_mapConfirms.erase(m_mapConfirms.begin(),m_mapConfirms.find(height));
    }
private:
    std::map<int,ConfirmMap> m_mapConfirms;
};


class CConfirmState
{
public:
    CConfirmState(){}

    bool InsertConfirm(const CBlockConfirm &confirm,CNode *node=nullptr)
    {
        if(node== nullptr)
            return m_selfConfirm.InsertConfirm(confirm);

        auto nodeConfirms = m_mapConfirm.find(node);
        if(nodeConfirms == m_mapConfirm.end())
            return m_mapConfirm.emplace(std::make_pair(node,CNodeConfirm(confirm))).second;
        else
            return nodeConfirms->second.InsertConfirm(confirm);
    }

    bool HaveConfirm(const CBlockConfirm &confirm,CNode *node = nullptr)
    {
        if(node == nullptr)
            return m_selfConfirm.HaveConfirm(confirm);
        auto nodeConfirms = m_mapConfirm.find(node);
        if(nodeConfirms != m_mapConfirm.end())
            return nodeConfirms->second.HaveConfirm(confirm);
        return false;
    }

    bool RemoveConfirms(int height)
    {
        for(auto & nodeConfirm : m_mapConfirm)
            nodeConfirm.second.RemoveConfirms(height);
        return true;
    }
private:
    CNodeConfirm m_selfConfirm;
    std::map<CNode *,CNodeConfirm> m_mapConfirm;

}g_confirmState;


static std::shared_ptr<const CBlockConfirm> GenerateConfirm(const std::shared_ptr<const CBlock> block)
{
    std::string witnessId;
    if(!strlen(localIp))
        get_local_ip("ens33",localIp);
    witnessId += localIp + std::string(":") + boost::lexical_cast<std::string>(gArgs.GetArg("-port",200));
    CBlockIndex *pindex = LookupBlockIndex(block->GetHash());
    return std::make_shared<const CBlockConfirm>(CBlockConfirm(block->GetHash(),witnessId,pindex?pindex->nHeight:-1,true));
}

static bool RelayConfirm(const std::shared_ptr<const CBlockConfirm> confirm)
{
    if(confirm->IsValid())
        GetMainSignals().RelayConfirm(confirm);
    return true;
}

static bool HaveConfirm(const std::shared_ptr<const CBlockConfirm> confirm)
{
    return g_confirmState.HaveConfirm(*confirm);
}

static void AddConfirmToCache(const std::shared_ptr<const CBlockConfirm> confirm)
{

}

static void UpdateConfirmCache(const std::shared_ptr<const CBlockConfirm> confirm = std::shared_ptr<const CBlockConfirm>())
{
}

static bool AddConfirm(const std::shared_ptr<const CBlockConfirm> confirm,const CNode *node = nullptr)
{
    if(HaveConfirm(confirm))
        return false;
    AddConfirmToCache(confirm);
    UpdateConfirmCache(confirm);
    return true;
}

/***************************************************
 * 1.检验发送者是否为见证者
 * 2.检查该见证者是否发送相同块的不同信息
 * 3.检查高度是否合适
 **************************************************/

static bool CheckWitness(const std::shared_ptr<const CBlockConfirm> confirm)
{
    return true;
    return error("%s: Confirmation : %s : CheckWitness failed ", __func__,confirm->ToString().c_str());

}
static bool HaveDiffConfirm(const std::shared_ptr<const CBlockConfirm> confirm)
{
    return false;
    return error("%s: Confirmation : %s : Had send diff confirm",__func__,confirm->ToString().c_str());

}
static bool ConfirmHaveRightHeigth(const std::shared_ptr<const CBlockConfirm> confirm)
{
    return true;
    return error("%s: Confirmation : %s : Height is not suitable",__func__,confirm->ToString().c_str());
}

static bool CheckConfirm(const std::shared_ptr<const CBlockConfirm> confirm)
{
    if(!CheckWitness(confirm))
        return false;
    if(HaveDiffConfirm(confirm))
        return false;
    if(!ConfirmHaveRightHeigth(confirm))
        return false;
    return true;
}


static bool AcceptConfirm(const std::shared_ptr<const CBlockConfirm> confirm,const CNode *node=nullptr)
{
    if(!CheckConfirm(confirm))
        return false;
    RelayConfirm(confirm);
    return AddConfirm(confirm,node);
}

/***************************************************
 * 函数说明：
 * 1.更新主链
 **************************************************/
static bool HaveCandidateBlock()
{
    return false;
}
static void ReleaseConfirmCache()
{
}
static bool ActivateBestChainBft()
{
    if(HaveCandidateBlock())
    {
        CValidationState state; // Only used to report errors, not invalidity - ignore it
        if (!ActivateBestChain(state,Params()))
            return error("%s: ActivateBestChain failed (%s)", __func__, FormatStateMessage(state));
        ReleaseConfirmCache();
    }
    return true;
}

/*************************************************外部可以调用的函数************************************/
bool NodeHaveConfirm(const NodeId &id,const CBlockConfirm &confirm)
{
    return true;
}

bool ProcessConfirm(const std::shared_ptr<const CBlockConfirm> &confirm,const CNode *node)
{
    if(!AcceptConfirm(confirm,node))
        return false;
    return ActivateBestChainBft();
}

bool ProcessNewBlockBft(const CChainParams& chainparams, const std::shared_ptr<const CBlock> pblock,bool fForceProcessing, bool *fNewBlock)
{
    if(!ProcessNewBlock(chainparams,pblock,fForceProcessing,fNewBlock))
        return false;
    return AcceptConfirm(GenerateConfirm(pblock));
}
