#include "omnicore/script.h"

#include "amount.h"
#include "script/script.h"
#include "script/standard.h"
#include "serialize.h"
#include "utilstrencodings.h"
#include "policy/feerate.h"
#include "policy/policy.h"
#include "pubkey.h"

#include <boost/foreach.hpp>

#include <string>
#include <utility>
#include <vector>

/** The minimum transaction relay fee. */
extern CFeeRate minRelayTxFee;

typedef std::vector<unsigned char> valtype;

/**
 * Determines the minimum output amount to be spent by an output, based on the
 * scriptPubKey size in relation to the minimum relay fee.
 *
 * @param scriptPubKey[in]  The scriptPubKey
 * @return The dust threshold value
 */
int64_t GetDustThreshold(const CScript& scriptPubKey)
{
    CTxOut txOut(0, scriptPubKey);

    return GetDustThreshold(txOut, minRelayTxFee);
}

/**
 * Identifies standard output types based on a scriptPubKey.
 *
 * Note: whichTypeRet is set to TX_NONSTANDARD, if no standard script was found.
 *
 * @param scriptPubKey[in]   The script
 * @param whichTypeRet[out]  The output type
 * @return True if a standard script was found
 */
bool GetOutputType(const CScript& scriptPubKey, txnouttype& whichTypeRet)
{
    std::vector<std::vector<unsigned char> > vSolutions;

    if (SafeSolver(scriptPubKey, whichTypeRet, vSolutions)) {
        return true;
    }
    whichTypeRet = TX_NONSTANDARD;

    return false;
}

/**
 * Extracts the pushed data as hex-encoded string from a script.
 *
 * @param script[in]      The script
 * @param vstrRet[out]    The extracted pushed data as hex-encoded string
 * @param fSkipFirst[in]  Whether the first push operation should be skipped (default: false)
 * @return True if the extraction was successful (result can be empty)
 */
bool GetScriptPushes(const CScript& script, std::vector<std::string>& vstrRet, bool fSkipFirst)
{
    int count = 0;
    CScript::const_iterator pc = script.begin();

    while (pc < script.end()) {
        opcodetype opcode;
        std::vector<unsigned char> data;
        if (!script.GetOp(pc, opcode, data))
            return false;
        if (0x00 <= opcode && opcode <= OP_PUSHDATA4)
            if (count++ || !fSkipFirst) vstrRet.push_back(HexStr(data));
    }

    return true;
}

static bool MatchPayToPubkey(const CScript& script, valtype& pubkey)
{
    if (script.size() == CPubKey::PUBLIC_KEY_SIZE + 2 && script[0] == CPubKey::PUBLIC_KEY_SIZE && script.back() == OP_CHECKSIG) {
      pubkey = valtype(script.begin() + 1, script.begin() + CPubKey::PUBLIC_KEY_SIZE + 1); 
      return CPubKey::ValidSize(pubkey);
    }   
    if (script.size() == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 2 && script[0] == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE && script.back() == OP_CHECKSIG) {
      pubkey = valtype(script.begin() + 1, script.begin() + CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 1); 
      return CPubKey::ValidSize(pubkey);
    }   
    return false;
}

static bool MatchPayToPubkeyHash(const CScript& script, valtype& pubkeyhash)
{
    if (script.size() == 25 && script[0] == OP_DUP && script[1] == OP_HASH160 && script[2] == 20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG) {
      pubkeyhash = valtype(script.begin () + 3, script.begin() + 23);
      return true;
    }   
    return false;
}

static constexpr bool IsSmallInteger(opcodetype opcode)
{
    return opcode >= OP_1 && opcode <= OP_16;
}

static bool MatchMultisig(const CScript& script, unsigned int& required, std::vector<valtype>& pubkeys)
{
    opcodetype opcode;
    valtype data;
    CScript::const_iterator it = script.begin();
    if (script.size() < 1 || script.back() != OP_CHECKMULTISIG) return false;

    if (!script.GetOp(it, opcode, data) || !IsSmallInteger(opcode)) return false;
    required = CScript::DecodeOP_N(opcode);
    while (script.GetOp(it, opcode, data) && CPubKey::ValidSize(data)) {
      pubkeys.emplace_back(std::move(data));
    }
    if (!IsSmallInteger(opcode)) return false;
    unsigned int keys = CScript::DecodeOP_N(opcode);
    if (pubkeys.size() != keys || keys < required) return false;
    return (it + 1 == script.end());
}

/**
 * Returns public keys or hashes from scriptPubKey, for standard transaction types.
 *
 * Note: in contrast to the script/standard/Solver, this Solver is not affected by
 * user settings, and in particular any OP_RETURN size is considered as standard.
 *
 * @param scriptPubKey[in]    The script
 * @param typeRet[out]        The output type
 * @param vSolutionsRet[out]  The extracted public keys or hashes
 * @return True if a standard script was found
 */
bool SafeSolver(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet)
{
    vSolutionsRet.clear();

    // Shortcut for pay-to-script-hash, which are more constrained than the other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    if (scriptPubKey.IsPayToScriptHash())
    {
        typeRet = TX_SCRIPTHASH;
        std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
        vSolutionsRet.push_back(hashBytes);
        return true;
    }

    int witnessversion;
    std::vector<unsigned char> witnessprogram;
    if (scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
        if (witnessversion == 0 && witnessprogram.size() == 20) {
            typeRet = TX_WITNESS_V0_KEYHASH;
            vSolutionsRet.push_back(witnessprogram);
            return true;
        }
        if (witnessversion == 0 && witnessprogram.size() == 32) {
            typeRet = TX_WITNESS_V0_SCRIPTHASH;
            vSolutionsRet.push_back(witnessprogram);
            return true;
        }
        return false;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (scriptPubKey.size() >= 2 && scriptPubKey[0] == OP_RETURN)
    {
        CScript script(scriptPubKey.begin()+1, scriptPubKey.end());
        if (script.IsPushOnly()) {
            typeRet = TX_NULL_DATA;
            return true;
        }
    }

    std::vector<unsigned char> data;
    if (MatchPayToPubkey(scriptPubKey, data)) {
      typeRet = TX_PUBKEY;
      vSolutionsRet.push_back(std::move(data));
      return true;
    }

    if (MatchPayToPubkeyHash(scriptPubKey, data)) {
      typeRet = TX_PUBKEYHASH;
      vSolutionsRet.push_back(std::move(data));
      return true;
    }

    unsigned int required;
    std::vector<std::vector<unsigned char>> keys;
    if (MatchMultisig(scriptPubKey, required, keys)) {
      typeRet = TX_MULTISIG;
      vSolutionsRet.push_back({static_cast<unsigned char>(required)}); // safe as required is in range 1..16
      vSolutionsRet.insert(vSolutionsRet.end(), keys.begin(), keys.end());
      vSolutionsRet.push_back({static_cast<unsigned char>(keys.size())}); // safe as size is in range 1..16
      return true;
    }

    vSolutionsRet.clear();
    typeRet = TX_NONSTANDARD;
    return false;
}
