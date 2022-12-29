FROM arm32v6/alpine

RUN apk upgrade --no-cache && apk add --no-cache libstdc++ bluez-libs libcurl yaml-cpp && ln -s /usr/lib/libcurl.so.4 /usr/lib/libcurl-gnutls.so.4 && mkdir -p /usr/local/bin
COPY bin/release/blescan /usr/local/bin
COPY config.yaml /usr/local/bin/config.yaml
CMD /usr/local/bin/blescan
