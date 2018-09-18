#ifndef BLOCKCONFIRM_H
#define BLOCKCONFIRM_H

#include <primitives/block.h>
#include <tinyformat.h>

#include <iostream>
#include <set>
#include <boost/lexical_cast.hpp>

typedef uint32_t WitnessId;

class CBlockConfirm
{
public:
    CBlockConfirm(){m_hashBlock.SetNull();m_confirm = false;}
    CBlockConfirm(const uint256 &hashBlockIn,const WitnessId &nodeIdIn,const uint32_t &heightIn,bool confirmIn = true):
        m_hashBlock(hashBlockIn),
        m_witnessId(nodeIdIn),
        m_height(heightIn),
        m_confirm(confirmIn){}
    void SetNull(){m_hashBlock.SetNull();}
    bool IsNull() const {return m_hashBlock.IsNull();}
    uint256 Hash() const {return m_hashBlock;}
    void SetHash(const uint256 hashBlock){m_hashBlock = hashBlock;}
    WitnessId Id() const {return m_witnessId;}
    void SetId(const WitnessId & id){m_witnessId = id;}
    bool Confirm()const {return m_confirm;}
    void SetConfirm(bool confirm){m_confirm = confirm;}
    uint32_t Height() const {return m_height;}
    void SetHeight(uint32_t height){m_height = height;}

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(m_hashBlock);
        READWRITE(m_witnessId);
        READWRITE(m_height);
        READWRITE(m_confirm);
    }
    friend bool operator<(const CBlockConfirm & a, const CBlockConfirm& b){return a.m_witnessId < b.m_witnessId;}

private:
    uint256 m_hashBlock;
    WitnessId  m_witnessId;
    uint32_t m_height;
    bool m_confirm;
};

class CNode;
class CChainParams;
typedef int64_t NodeId;

bool NodeHaveConfirm(const NodeId &id,const WitnessId &witnessId);
bool ProcessConfirm(const std::shared_ptr<const CBlockConfirm> &confirm, const CNode *node);
bool ProcessNewBlockBft(const CChainParams& chainparams, const std::shared_ptr<const CBlock> pblock, bool fForceProcessing, bool *fNewBlock);
#endif // BLOCKCONFIRM_H
