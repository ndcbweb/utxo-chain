// Nadcab custom chain: every consensus rule of the network is read from the -chainjson file.

#include <arith_uint256.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <univalue.h>
#include <util/fs.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

int64_t JsonInt(const UniValue& obj, std::string_view key, int64_t fallback)
{
    const UniValue& value = obj.find_value(key);
    if (value.isNull()) return fallback;
    if (value.isNum()) return value.getInt<int64_t>();
    return std::stoll(value.get_str());
}

std::string JsonStr(const UniValue& obj, std::string_view key, const std::string& fallback)
{
    const UniValue& value = obj.find_value(key);
    return value.isStr() ? value.get_str() : fallback;
}

std::vector<unsigned char> JsonHex(const UniValue& obj, std::string_view key, size_t size)
{
    const auto bytes = TryParseHex<unsigned char>(JsonStr(obj, key, ""));
    if (!bytes || bytes->size() != size) {
        throw std::runtime_error(strprintf("chainjson: %s must be %u hex bytes", std::string{key}, size));
    }
    return *bytes;
}

class CNadcabParams : public CChainParams
{
public:
    CNadcabParams(const UniValue& json, bool print_genesis)
    {
        m_chain_type = ChainType::MAIN;
        const std::string algorithm = JsonStr(json, "algorithm", "sha256d");
        if (algorithm != "sha256d" && algorithm != "scrypt") throw std::runtime_error("chainjson: algorithm must be sha256d or scrypt");
        g_scrypt_pow = algorithm == "scrypt";

        const UniValue& subsidy = json.find_value("subsidy");
        const UniValue& genesis_json = json.find_value("genesis");
        if (!subsidy.isObject() || !genesis_json.isObject()) throw std::runtime_error("chainjson: subsidy and genesis objects are required");

        const int64_t spacing = JsonInt(json, "blockSpacing", 600);
        const int64_t retarget = JsonInt(json, "retargetInterval", 2016);
        if (spacing < 1 || spacing > 86400 || retarget < 1 || retarget > 100000) throw std::runtime_error("chainjson: invalid block spacing or retarget interval");

        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = JsonInt(subsidy, "halvingInterval", 210000);
        consensus.nInitialSubsidy = JsonInt(subsidy, "initial", 50 * COIN);
        consensus.nMaxHalvings = JsonInt(subsidy, "maxHalvings", 64);
        consensus.nReductionPercent = JsonInt(subsidy, "reductionPercent", 50);
        if (consensus.nReductionPercent < 1 || consensus.nReductionPercent > 99 || consensus.nMaxHalvings < 0 || consensus.nMaxHalvings > 10000) {
            throw std::runtime_error("chainjson: reductionPercent must be 1-99 and maxHalvings 0-10000");
        }
        consensus.nTailSubsidy = JsonInt(subsidy, "tail", 0);
        consensus.nPremine = JsonInt(subsidy, "premine", 0);
        if (!MoneyRange(consensus.nInitialSubsidy) || !MoneyRange(consensus.nTailSubsidy) || !MoneyRange(consensus.nPremine)) {
            throw std::runtime_error("chainjson: subsidy amounts out of range");
        }
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        const auto pow_limit = uint256::FromHex(JsonStr(json, "powLimit", ""));
        if (!pow_limit) throw std::runtime_error("chainjson: powLimit must be 64 hex characters");
        consensus.powLimit = *pow_limit;
        consensus.nPowTargetSpacing = spacing;
        consensus.nPowTargetTimespan = spacing * retarget;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.nMinerConfirmationWindow = retarget;
        consensus.nRuleChangeActivationThreshold = (retarget * 95 + 99) / 100;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0;

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        const auto magic = JsonHex(json, "messageStart", 4);
        std::copy(magic.begin(), magic.end(), pchMessageStart.begin());
        nDefaultPort = JsonInt(json, "port", 8333);
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        const std::string message = JsonStr(genesis_json, "message", "Nadcab Labs genesis");
        const auto output_key = TryParseHex<unsigned char>(JsonStr(genesis_json, "outputKey",
            "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f"));
        if (!output_key) throw std::runtime_error("chainjson: genesis.outputKey must be hex");

        CMutableTransaction coinbase;
        coinbase.version = 1;
        coinbase.vin.resize(1);
        coinbase.vout.resize(1);
        coinbase.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4)
                                              << std::vector<unsigned char>(message.begin(), message.end());
        coinbase.vout[0].nValue = consensus.nInitialSubsidy;
        coinbase.vout[0].scriptPubKey = CScript() << *output_key << OP_CHECKSIG;

        genesis.nVersion = 1;
        genesis.nTime = JsonInt(genesis_json, "time", 1231006505);
        genesis.nBits = std::stoul(JsonStr(genesis_json, "bits", "1d00ffff"), nullptr, 16);
        genesis.nNonce = JsonInt(genesis_json, "nonce", 0);
        genesis.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
        genesis.hashPrevBlock.SetNull();
        genesis.hashMerkleRoot = BlockMerkleRoot(genesis);

        if (print_genesis || genesis_json.find_value("nonce").isNull()) {
            arith_uint256 target;
            bool negative, overflow;
            target.SetCompact(genesis.nBits, &negative, &overflow);
            if (negative || overflow || target == 0 || target > UintToArith256(consensus.powLimit)) {
                throw std::runtime_error("chainjson: genesis bits are outside powLimit");
            }
            while (UintToArith256(genesis.GetPoWHash()) > target) {
                if (++genesis.nNonce == 0) ++genesis.nTime;
            }
        }
        consensus.hashGenesisBlock = genesis.GetHash();

        if (print_genesis) {
            std::cout << strprintf("{\"time\":%u,\"bits\":\"%08x\",\"nonce\":%u,\"hash\":\"%s\",\"powHash\":\"%s\",\"merkleRoot\":\"%s\"}\n",
                                   genesis.nTime, genesis.nBits, genesis.nNonce, consensus.hashGenesisBlock.GetHex(),
                                   genesis.GetPoWHash().GetHex(), genesis.hashMerkleRoot.GetHex());
            std::exit(0);
        }
        const std::string expected = JsonStr(genesis_json, "hash", "");
        if (!expected.empty() && expected != consensus.hashGenesisBlock.GetHex()) {
            throw std::runtime_error(strprintf("chainjson: genesis hash %s does not match %s", consensus.hashGenesisBlock.GetHex(), expected));
        }

        vFixedSeeds.clear();
        vSeeds.clear();

        const UniValue& base58 = json.find_value("base58");
        if (!base58.isObject()) throw std::runtime_error("chainjson: base58 object is required");
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, JsonInt(base58, "pubkey", 0));
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, JsonInt(base58, "script", 5));
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, JsonInt(base58, "secret", 128));
        base58Prefixes[EXT_PUBLIC_KEY] = JsonHex(base58, "extPublic", 4);
        base58Prefixes[EXT_SECRET_KEY] = JsonHex(base58, "extSecret", 4);
        bech32_hrp = JsonStr(json, "bech32", "nc");

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;
        checkpointData = {{{0, consensus.hashGenesisBlock}}};
        m_assumeutxo_data = {};
        chainTxData = ChainTxData{genesis.nTime, 1, 0};
    }
};

std::unique_ptr<const CChainParams> NadcabParams(const ArgsManager& args)
{
    const std::string path = fs::PathToString(args.GetPathArg("-chainjson"));
    std::ifstream file{path};
    if (!file) throw std::runtime_error(strprintf("chainjson: cannot read %s", path));
    std::stringstream buffer;
    buffer << file.rdbuf();
    UniValue json;
    if (!json.read(buffer.str()) || !json.isObject()) throw std::runtime_error(strprintf("chainjson: %s is not a JSON object", path));
    return std::make_unique<CNadcabParams>(json, args.GetBoolArg("-printgenesis", false));
}

} // namespace

