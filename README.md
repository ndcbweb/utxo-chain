# utxo-chain

Bitcoin Core v28.1 with one change in purpose: the whole network is defined by a JSON file, so Nadcab Console can launch a
new UTXO proof-of-work chain (or a Bitcoin fork) without recompiling.

```
nadcab-coind -chainjson=/chain/chain.json -datadir=/data
```

## Chain file

| Field | Meaning |
| --- | --- |
| `algorithm` | `sha256d` (Bitcoin) or `scrypt` (Litecoin, N=1024 r=1 p=1) |
| `messageStart` | 4 network magic bytes (hex) |
| `port` | default P2P port |
| `bech32` | SegWit address prefix (`bc` on Bitcoin) |
| `base58` | `pubkey`, `script`, `secret` version bytes; `extPublic`, `extSecret` 4-byte xpub/xprv versions (hex) |
| `blockSpacing` | target seconds per block |
| `retargetInterval` | blocks between difficulty adjustments |
| `powLimit` | easiest allowed target (64 hex characters) |
| `genesis` | `time`, `bits` (initial difficulty), `message`, optional `outputKey`, `nonce`, `hash` |
| `subsidy` | `initial` reward, `halvingInterval`, `maxHalvings`, `tail` minimum reward, block-1 `premine` (satoshi strings) |

`nadcab-genesis /chain/chain.json` mines the genesis block and writes `nonce`, `time` and `hash` back into the file.
Every node of the network must use the same file.

Fixed rules: SegWit, Taproot, CSV, CLTV and BIP34 are active from the start, coinbase maturity is 100 blocks, block
weight is 4 MW, and the largest valid amount is 40 billion coins.

## Image

`.github/workflows/release.yml` builds the image, runs `test/integration.sh` and publishes it to the `latest` release:

```
curl -fsSL https://github.com/ndcbweb/utxo-chain/releases/download/latest/utxo-chain.tar.gz | gunzip | docker load
```

Tools in the image: `nadcab-coind`, `nadcab-coin-cli`, `nadcab-genesis`, `nadcab-miner`.
