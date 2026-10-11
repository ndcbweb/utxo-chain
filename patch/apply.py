#!/usr/bin/env python3
"""Turns a Bitcoin Core v28.1 source tree into nadcab-coind: chain rules from -chainjson, SHA-256d or Scrypt PoW."""
import pathlib
import shutil
import sys

ROOT = pathlib.Path(sys.argv[1])
HERE = pathlib.Path(__file__).resolve().parent


def edit(rel, old, new, count=1):
    path = ROOT / rel
    text = path.read_text()
    found = text.count(old)
    if found != count:
        sys.exit(f"patch: expected {count} match(es) in {rel}, found {found}:\n{old}")
    path.write_text(text.replace(old, new))


# Scrypt proof of work hash, selected at runtime by the chain file.
shutil.copy(HERE / "scrypt.h", ROOT / "src/crypto/scrypt.h")
shutil.copy(HERE / "scrypt.cpp", ROOT / "src/crypto/scrypt.cpp")
edit("src/Makefile.am", "  crypto/hmac_sha512.h \\\n", "  crypto/hmac_sha512.h \\\n  crypto/scrypt.cpp \\\n  crypto/scrypt.h \\\n")
edit("src/primitives/block.h", "    uint256 GetHash() const;\n", "    uint256 GetHash() const;\n    /** Hash checked against the target: SHA-256d, or Scrypt when g_scrypt_pow is set. */\n    uint256 GetPoWHash() const;\n")
edit("src/primitives/block.h", "class CBlockHeader\n", "/** Set by a -chainjson network whose algorithm is scrypt. */\nextern bool g_scrypt_pow;\n\nclass CBlockHeader\n")
edit(
    "src/primitives/block.cpp",
    "uint256 CBlockHeader::GetHash() const\n",
    """bool g_scrypt_pow{false};

uint256 CBlockHeader::GetPoWHash() const
{
    if (!g_scrypt_pow) return GetHash();
    unsigned char header[80];
    WriteLE32(header, uint32_t(nVersion));
    std::memcpy(header + 4, hashPrevBlock.begin(), 32);
    std::memcpy(header + 36, hashMerkleRoot.begin(), 32);
    WriteLE32(header + 68, nTime);
    WriteLE32(header + 72, nBits);
    WriteLE32(header + 76, nNonce);
    uint256 hash;
    scrypt_1024_1_1_256(header, hash.begin());
    return hash;
}

uint256 CBlockHeader::GetHash() const
""",
)
edit("src/primitives/block.cpp", "#include <hash.h>\n", "#include <crypto/common.h>\n#include <crypto/scrypt.h>\n#include <hash.h>\n\n#include <cstring>\n")

edit("src/validation.cpp", "CheckProofOfWork(block.GetHash(), block.nBits, consensusParams)", "CheckProofOfWork(block.GetPoWHash(), block.nBits, consensusParams)")
edit("src/validation.cpp", "CheckProofOfWork(header.GetHash(), header.nBits, consensusParams)", "CheckProofOfWork(header.GetPoWHash(), header.nBits, consensusParams)")
edit("src/rpc/mining.cpp", "CheckProofOfWork(block.GetHash(), block.nBits, chainman.GetConsensus())", "CheckProofOfWork(block.GetPoWHash(), block.nBits, chainman.GetConsensus())")
edit("src/node/blockstorage.cpp", "CheckProofOfWork(block.GetHash(), block.nBits, GetConsensus())", "CheckProofOfWork(block.GetPoWHash(), block.nBits, GetConsensus())")
edit("src/node/blockstorage.cpp", "CheckProofOfWork(pindexNew->GetBlockHash(), pindexNew->nBits, consensusParams)", "CheckProofOfWork(pindexNew->GetBlockHeader().GetPoWHash(), pindexNew->nBits, consensusParams)")

# Emission: initial reward, halving interval, halving cap, tail emission and a block-1 premine.
edit(
    "src/consensus/params.h",
    "    int nSubsidyHalvingInterval;\n",
    """    int nSubsidyHalvingInterval;
    /** Nadcab emission rules (defaults keep Bitcoin's schedule). */
    int64_t nInitialSubsidy{50 * 100000000LL};
    int nMaxHalvings{64};
    /** Percent cut at each reduction step; 50 is an exact halving. */
    int nReductionPercent{50};
    int64_t nTailSubsidy{0};
    int64_t nPremine{0};
""",
)
edit(
    "src/validation.cpp",
    """    int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
        return 0;

    CAmount nSubsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years.
    nSubsidy >>= halvings;
    return nSubsidy;
""",
    """    if (nHeight == 1 && consensusParams.nPremine > 0) return consensusParams.nPremine;
    int steps = consensusParams.nSubsidyHalvingInterval > 0 ? nHeight / consensusParams.nSubsidyHalvingInterval : 0;
    steps = std::min(steps, consensusParams.nMaxHalvings);
    CAmount nSubsidy = consensusParams.nInitialSubsidy;
    const int keep = 100 - consensusParams.nReductionPercent;
    if (consensusParams.nReductionPercent == 50) {
        nSubsidy = steps >= 64 ? 0 : (nSubsidy >> steps);
    } else {
        for (int i = 0; i < steps && nSubsidy > 0; ++i) nSubsidy = nSubsidy / 100 * keep + (nSubsidy % 100) * keep / 100;
    }
    return std::max<CAmount>(nSubsidy, consensusParams.nTailSubsidy);
""",
)

# Room for large supplies; twice MAX_MONEY must still fit in int64 when outputs are summed.
edit("src/consensus/amount.h", "static constexpr CAmount MAX_MONEY = 21000000 * COIN;", "static constexpr CAmount MAX_MONEY = 40000000000LL * COIN;")

# Chain selection.
edit(
    "src/chainparamsbase.cpp",
    '    argsman.AddArg("-chain=<chain>",',
    """    argsman.AddArg("-chainjson=<file>", "Run the custom network whose consensus rules are in <file> (Nadcab)", ArgsManager::ALLOW_ANY, OptionsCategory::CHAINPARAMS);
    argsman.AddArg("-printgenesis", "Mine the -chainjson genesis block, print it as JSON and exit (Nadcab)", ArgsManager::ALLOW_ANY, OptionsCategory::CHAINPARAMS);
    argsman.AddArg("-chain=<chain>",""",
)
edit(
    "src/chainparams.cpp",
    "static std::unique_ptr<const CChainParams> globalChainParams;\n",
    (HERE / "nadcab_params.inc").read_text() + "static std::unique_ptr<const CChainParams> globalChainParams;\n",
)
edit(
    "src/chainparams.cpp",
    "    case ChainType::MAIN:\n        return CChainParams::Main();\n",
    "    case ChainType::MAIN:\n        if (args.IsArgSet(\"-chainjson\")) return NadcabParams(args);\n        return CChainParams::Main();\n",
)

print("nadcab patch applied")
