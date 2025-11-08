#syntax=nobidev/dockerfile
FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive

ENV CEF_ROOT=/opt/cef
ENV NODE_ADDON_API_DIR=/usr/local/include/node-addon-api

RUN <<-EOF
	apt-get update
	apt-get install -y --no-install-recommends ca-certificates curl wget git build-essential cmake python3 python3-dev pkg-config g++ unzip tar bzip2
	curl -fsL https://deb.nodesource.com/setup_20.x | bash -s - -y
	apt-get install -y --no-install-recommends nodejs libc6-dev libgtk-3-dev libgconf-2-4 libnss3 libasound2 libx11-6 libx11-xcb1 libxss1 libatk1.0-0 libcups2 libdbus-1-3 libxcb1 libxcomposite1 libxdamage1 libxfixes3 libxrandr2 libgtk-3-dev libxrender1 libxext6 libglib2.0-dev
	rm -rf /var/lib/apt/lists/*
EOF

WORKDIR /build
COPY package.json package-lock.json ./
RUN npm install

ENV CEF_VERSION='142.0.10+g29548e2'
ENV CHROME_VERSION='142.0.7444.135'
RUN <<-EOF
	mkdir -p ${CEF_ROOT}/
	url="https://cef-builds.spotifycdn.com/cef_binary_$(echo "import urllib.parse; print(urllib.parse.quote('${CEF_VERSION}+chromium-${CHROME_VERSION}'))" | python3)_linux64.tar.bz2"
	curl -fsL "${url}" | tar -xj -C ${CEF_ROOT} --strip-components=1
EOF

COPY ./ ./

RUN cmake .
RUN make || true
