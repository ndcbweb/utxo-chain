FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential libtool autotools-dev automake pkg-config bsdmainutils python3 \
      libevent-dev libboost-dev libsqlite3-dev curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ARG BITCOIN_VERSION=28.1
WORKDIR /src
RUN curl -fsSL "https://github.com/bitcoin/bitcoin/archive/refs/tags/v${BITCOIN_VERSION}.tar.gz" | tar -xz --strip-components=1

COPY patch /patch
RUN python3 /patch/apply.py /src

RUN ./autogen.sh \
    && ./configure --disable-tests --disable-bench --disable-fuzz-binary --disable-man --without-gui \
         --without-bdb --with-sqlite=yes --without-miniupnpc --without-natpmp --disable-zmq \
         CXXFLAGS="-O2 -g0" \
    && make -C src -j"$(nproc)" bitcoind bitcoin-cli \
    && strip src/bitcoind src/bitcoin-cli

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
      libevent-2.1-7 libevent-pthreads-2.1-7 libsqlite3-0 jq ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/src/bitcoind /usr/local/bin/nadcab-coind
COPY --from=build /src/src/bitcoin-cli /usr/local/bin/nadcab-coin-cli
COPY scripts/ /usr/local/bin/
RUN chmod 0755 /usr/local/bin/*

VOLUME ["/data"]
ENTRYPOINT ["nadcab-coind"]
