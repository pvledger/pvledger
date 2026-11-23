FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake g++ git libjsoncpp-dev libsqlite3-dev libssl-dev make pkg-config \
    && rm -rf /var/lib/apt/lists/*
ARG BUILD_JOBS=4
WORKDIR /source
COPY CMakeLists.txt LICENSE ./
COPY cmake ./cmake
COPY src ./src
COPY templates ./templates
COPY js ./js
COPY lang ./lang
COPY themes ./themes
COPY db/pvledger.sql ./db/pvledger.sql
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build build --parallel "${BUILD_JOBS}" \
    && DESTDIR=/stage cmake --install build

FROM debian:bookworm-slim
RUN groupadd --system pvledger && useradd --system --gid pvledger pvledger \
    && install -d -m 700 -o pvledger -g pvledger /var/lib/pvledger
COPY --from=build /stage/usr/local/bin/pvledger /usr/local/bin/pvledger
COPY --from=build /stage/usr/local/share/pvledger /usr/local/share/pvledger
USER pvledger
WORKDIR /var/lib/pvledger
ENTRYPOINT ["pvledger"]
CMD ["-c", "/var/lib/pvledger/config.yaml"]
