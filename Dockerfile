FROM ubuntu:22.04

# basic deps
RUN apt-get update && apt-get install -y \
    g++ \
    make \
    curl \
    libcurl4-openssl-dev \
    nlohmann-json3-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN make

EXPOSE 10000

CMD ["./app"]
