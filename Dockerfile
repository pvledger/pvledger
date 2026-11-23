FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake g++ git libjsoncpp-dev libsqlite3-dev libssl-dev make pkg-config \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY CMakeLists.txt ./
COPY src ./src
COPY templates ./templates
COPY js ./js
COPY lang ./lang
COPY themes ./themes
COPY db/pvledger.sql ./db/pvledger.sql
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build build --parallel \
    && DESTDIR=/stage cmake --install build

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libjsoncpp25 libsqlite3-0 libssl3 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system pvledger && useradd --system --gid pvledger pvledger \
    && install -d -m 700 -o pvledger -g pvledger /var/lib/pvledger
COPY --from=build /stage/usr/local/bin/vpledger /usr/local/bin/vpledger
COPY --from=build /stage/usr/local/share/pvledger /usr/local/share/pvledger
USER pvledger
WORKDIR /var/lib/pvledger
ENTRYPOINT ["vpledger"]
CMD ["-c", "/var/lib/pvledger/config.yaml"]
