#ifndef BLOCKCONFIRM_H
#define BLOCKCONFIRM_H

#include <primitives/block.h>
#include <tinyformat.h>

#include <iostream>
#include <set>
#include <boost/lexical_cast.hpp>

#define __HLOG__

#ifdef __HLOG__
#define HLOG(format,...) printf("FUNC: %s, LINE: %d: " format "\n",__FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define HLOG(format,...)
#endif

typedef std::string WitnessId;

class CBlockConfirm
{
public:
    CBlockConfirm(){m_hashBlock.SetNull();}
    CBlockConfirm(const uint256 &hashBlockIn,const WitnessId &nodeIdIn,const uint32_t &heightIn):
        m_hashBlock(hashBlockIn),
        m_witnessId(nodeIdIn),
        m_height(heightIn){}
    ~CBlockConfirm()
    {
        HLOG("cblockconfirm release.......");
    }
    void SetNull(){m_hashBlock.SetNull();}
    bool IsNull() const {return m_hashBlock.IsNull();}
    uint256 Hash() const {return m_hashBlock;}
    void SetHash(const uint256 hashBlock){m_hashBlock = hashBlock;}
    WitnessId Id() const {return m_witnessId;}
    void SetId(const WitnessId & id){m_witnessId = id;}
    int Height() const {return m_height;}
    void SetHeight(uint32_t height){m_height = height;}

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(m_hashBlock);
        READWRITE(m_witnessId);
        READWRITE(m_height);
    }
    friend bool operator <(const CBlockConfirm & a, const CBlockConfirm& b)
    {
        return (a.m_witnessId < b.m_witnessId || (a.m_witnessId == b.m_witnessId && a.m_hashBlock < b.m_hashBlock));
    }

    std::string ToString()const{return std::string();}


private:
    uint256 m_hashBlock;
    WitnessId  m_witnessId;
    uint32_t m_height;
};

class CNode;
class CChainParams;
typedef int64_t NodeId;

bool NodeHaveConfirm(const NodeId &id, const CBlockConfirm &confirm);
void AddConfirmToNode(const CBlockConfirm &confirm,const CNode *node);
bool ProcessConfirm(const std::shared_ptr<const CBlockConfirm> &confirm, const CNode *node);
bool ProcessNewBlockBft(const CChainParams& chainparams, const std::shared_ptr<const CBlock> pblock, bool fForceProcessing, bool *fNewBlock);
#endif // BLOCKCONFIRM_H
