#!/bin/bash
# End-to-end checks of the nadcab-coind image: emission rules, address prefixes, Scrypt PoW, P2P sync, restart, retarget.
set -euo pipefail
IMAGE="${IMAGE:-nadcab/utxo-chain:latest}"
WORK="$(mktemp -d)"
trap 'docker rm -f n1 n2 >/dev/null 2>&1 || true' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

chain_json() { # algorithm port magic [reductionPercent]
  cat <<JSON
{
  "name": "Test Coin", "ticker": "TST", "algorithm": "$1",
  "messageStart": "$3", "port": $2, "bech32": "tst",
  "base58": { "pubkey": 50, "script": 55, "secret": 178, "extPublic": "0488b21e", "extSecret": "0488ade4" },
  "blockSpacing": 600, "retargetInterval": 50,
  "powLimit": "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
  "genesis": { "time": 1760000000, "bits": "207fffff", "message": "Nadcab CI genesis" },
  "subsidy": { "initial": "5000000000", "halvingInterval": 20, "maxHalvings": 3, "tail": "100000000", "premine": "100000000000", "reductionPercent": ${4:-50} }
}
JSON
}

start_node() { # name chain-dir port rpcport [extra...]
  local name=$1 dir=$2 port=$3 rpc=$4; shift 4
  mkdir -p "$dir/data"
  docker run -d --name "$name" --network host -v "$dir:/chain" -v "$dir/data:/data" "$IMAGE" \
    -datadir=/data -chainjson=/chain/chain.json -port="$port" -rpcport="$rpc" -rpcbind=127.0.0.1 -rpcallowip=127.0.0.1 \
    -txindex=1 -fallbackfee=0.0001 -listen=1 -discover=0 -dnsseed=0 -printtoconsole=1 "$@" >/dev/null
  for _ in $(seq 1 60); do cli "$name" "$rpc" getblockcount >/dev/null 2>&1 && return; sleep 1; done
  docker logs "$name" | tail -40; fail "$name did not start"
}
cli() { local name=$1 rpc=$2; shift 2; docker exec "$name" nadcab-coin-cli -datadir=/data -rpcport="$rpc" "$@"; }
# Coinbase output total in satoshis (jq would print 1000.00000000 as 1000).
coinbase_value() { cli "$1" "$2" getblock "$(cli "$1" "$2" getblockhash "$3")" 2 | jq -r '[.tx[0].vout[].value * 100000000 | round] | add'; }

echo "== SHA-256d chain"
A="$WORK/a"; mkdir -p "$A"
chain_json sha256d 39333 f1c2d3e4 > "$A/chain.json"
docker run --rm -v "$A:/chain" --entrypoint nadcab-genesis "$IMAGE" /chain/chain.json
jq -e '.genesis.hash and (.genesis.nonce|type=="number")' "$A/chain.json" >/dev/null || fail "genesis not written"
start_node n1 "$A" 39333 39332
[ "$(cli n1 39332 getblockchaininfo | jq -r .chain)" = "main" ] || fail "chain type"
[ "$(cli n1 39332 getblockhash 0)" = "$(jq -r .genesis.hash "$A/chain.json")" ] || fail "genesis hash mismatch"
cli n1 39332 createwallet w >/dev/null
BECH="$(cli n1 39332 getnewaddress '' bech32)"; LEGACY="$(cli n1 39332 getnewaddress '' legacy)"
[[ "$BECH" == tst1* ]] || fail "bech32 prefix $BECH"
[[ "$LEGACY" == M* ]] || fail "base58 prefix $LEGACY"
echo "addresses ok: $BECH $LEGACY"

cli n1 39332 generatetoaddress 1 "$BECH" >/dev/null
[ "$(coinbase_value n1 39332 1)" = "100000000000" ] || fail "premine $(coinbase_value n1 39332 1)"
cli n1 39332 generatetoaddress 109 "$BECH" >/dev/null
if [ "$(cli n1 39332 getblockcount)" != 110 ]; then
  cli n1 39332 getchaintips
  docker logs n1 2>&1 | grep -iE "invalid|error|reject|bad-" | tail -20
  fail "mined to height $(cli n1 39332 getblockcount), want 110"
fi
for pair in 2:5000000000 19:5000000000 20:2500000000 40:1250000000 60:625000000 100:625000000; do
  h=${pair%%:*}; want=${pair#*:}
  got="$(coinbase_value n1 39332 "$h")"
  [ "$got" = "$want" ] || fail "subsidy at $h: $got, want $want"
done
echo "emission ok"

D10="$(cli n1 39332 getblockheader "$(cli n1 39332 getblockhash 99)" | jq -r .bits)"
D20="$(cli n1 39332 getblockheader "$(cli n1 39332 getblockhash 100)" | jq -r .bits)"
[ "$D10" != "$D20" ] || fail "difficulty did not retarget ($D10)"
echo "retarget ok: $D10 -> $D20"

cli n1 39332 sendtoaddress "$LEGACY" 12.5 >/dev/null
cli n1 39332 generatetoaddress 1 "$BECH" >/dev/null
[ "$(cli n1 39332 getreceivedbyaddress "$LEGACY")" = "12.50000000" ] || fail "spend"
echo "spend ok"

echo "== Scrypt chain, two nodes"
docker rm -f n1 >/dev/null
B="$WORK/b"; C="$WORK/c"; mkdir -p "$B" "$C"
chain_json scrypt 39433 a1b2c3d4 20 > "$B/chain.json"
docker run --rm -v "$B:/chain" --entrypoint nadcab-genesis "$IMAGE" /chain/chain.json
cp "$B/chain.json" "$C/chain.json"
start_node n1 "$B" 39433 39432
start_node n2 "$C" 39434 39442 -addnode=127.0.0.1:39433
cli n1 39432 createwallet w >/dev/null
ADDR="$(cli n1 39432 getnewaddress '' bech32)"
cli n1 39432 generatetoaddress 45 "$ADDR" >/dev/null
for _ in $(seq 1 60); do [ "$(cli n2 39442 getblockcount)" = "45" ] && break; sleep 1; done
[ "$(cli n2 39442 getbestblockhash)" = "$(cli n1 39432 getbestblockhash)" ] || fail "n2 did not sync"
echo "p2p sync ok"
for pair in 2:5000000000 20:4000000000 40:3200000000; do
  h=${pair%%:*}; want=${pair#*:}
  got="$(coinbase_value n1 39432 "$h")"
  [ "$got" = "$want" ] || fail "20% reduction at $h: $got, want $want"
done
echo "reduction schedule ok"

for h in 0 1 12 45; do
  HEADER="$(cli n1 39432 getblockheader "$(cli n1 39432 getblockhash $h)" false)"
  python3 - "$HEADER" <<'PY' || fail "scrypt hash of block $h above target"
import hashlib, sys
header = bytes.fromhex(sys.argv[1])
bits = int.from_bytes(header[72:76], "little")
target = (bits & 0xffffff) << (8 * ((bits >> 24) - 3))
pow_hash = int.from_bytes(hashlib.scrypt(header, salt=header, n=1024, r=1, p=1, dklen=32), "little")
assert pow_hash <= target, (hex(pow_hash), hex(target))
PY
done
echo "scrypt pow ok"

docker restart n2 >/dev/null
for _ in $(seq 1 60); do cli n2 39442 getblockcount >/dev/null 2>&1 && break; sleep 1; done
[ "$(cli n2 39442 getblockcount)" = "45" ] || fail "restart lost blocks"
echo "restart ok"

echo "== Public RPC whitelist"
docker rm -f n2 >/dev/null
SALT=0a1b2c3d4e5f
AUTH="$(python3 -c "import hmac,hashlib;print(hmac.new(b'$SALT', b'pubpass', hashlib.sha256).hexdigest())")"
start_node n2 "$C" 39434 39442 -addnode=127.0.0.1:39433 -rpcbind=127.0.0.1:39450 \
  "-rpcauth=public:$SALT\$$AUTH" -rpcwhitelist=public:getblockcount,getblockchaininfo -rpcwhitelistdefault=0
rpc() { curl -s -o /dev/null -w '%{http_code}' -u public:pubpass -H 'content-type: application/json' \
  -d "{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"$1\",\"params\":[]}" http://127.0.0.1:39450/; }
[ "$(rpc getblockcount)" = "200" ] || fail "public getblockcount"
[ "$(rpc stop)" = "403" ] || fail "public stop must be forbidden"
echo "rpc whitelist ok"
echo "all checks passed"
