# syntax=docker/dockerfile:1
FROM debian:buster-slim AS build

RUN apt update \
 && apt install -y build-essential cmake git \
                   libcurl4-openssl-dev libzip-dev liblua5.1-0-dev

RUN mkdir -p /usr/share/man/man1  # See: https://github.com/geerlingguy/ansible-role-java/issues/64#issuecomment-393299088
RUN apt install -y default-jdk zlib1g-dev libfreetype6-dev cmake \
                   libsdl2-dev libopenal-dev libglew-dev zip libvorbis-dev libxcursor-dev \
                   libdevil-dev libboost-system-dev libboost-thread-dev \
                   libboost-regex-dev libboost-serialization-dev \
                   libboost-program-options-dev libboost-chrono-dev \
                   libunwind-dev  libboost-filesystem-dev \
                   libboost-signals-dev libboost-test-dev \
                   xsltproc libfontconfig1-dev p7zip

RUN git clone --depth=1 https://github.com/spring/spring.git /spring-src

WORKDIR /spring-src

RUN cp /usr/bin/7zr /usr/bin/7z # See: https://github.com/spring/spring/blob/a8cf33ad1d2ac775e6008cd04baa7e859d1f23ec/rts/build/cmake/FindSevenZip.cmake#L26

RUN git submodule init && git submodule update && CI=1 cmake . && CI=1 make unitsync

WORKDIR /rapid-src

# RUN git clone https://github.com/spring/pr-downloader.git .

COPY cmake ./cmake
COPY src ./src
COPY test ./test
COPY ["CMakeLists.txt", "Doxyfile", "./"]

RUN git clone --depth=1 https://github.com/libgit2/libgit2.git src/lib/libgit2

RUN cmake -DRAPIDTOOLS=ON . && make pr-downloader -j2 && make all

FROM php:apache-buster AS scratch

RUN mv "$PHP_INI_DIR/php.ini-production" "$PHP_INI_DIR/php.ini"

RUN apt update \
 && apt install -y git gzip python3 \
                   libcurl4-openssl-dev libzip-dev liblua5.1-0-dev libminizip-dev \
                   python3-sqlalchemy python3-requests python3-mysqldb python3-pip python3-pil p7zip \
                   vim default-mysql-client

RUN pip3 install mysql-connector-python
RUN docker-php-ext-install mysqli

RUN mkdir -p /spring/engine/104.0.1-1952-gd9c289f\ bar \
 && cd /spring/engine/104.0.1-1952-gd9c289f\ bar \
 && curl -LJO  'https://github.com/beyond-all-reason/spring/releases/download/spring_bar_%7BBAR%7D104.0.1-1952-gd9c289f/spring_bar_.BAR.104.0.1-1952-gd9c289f_linux-64-minimal-portable.7z' \
 && 7zr x *.7z

ARG RAPID_GIT
ARG RAPID_PACKAGES
ARG RAPID_GIT_REPOS
ARG RAPID_FILES
ARG REPOS_HOSTNAME
ARG API_HOSTNAME
ARG UPQ_CREDENTIALS
ARG UPQ_JOBS

ENV RAPID_GIT=$RAPID_GIT
ENV RAPID_PACKAGES=$RAPID_PACKAGES
ENV RAPID_GIT_REPOS=$RAPID_GIT_REPOS
ENV RAPID_FILES=$RAPID_FILES
ENV REPOS_HOSTNAME=$REPOS_HOSTNAME
ENV API_HOSTNAME=$API_HOSTNAME
ENV UPQ_CREDENTIALS=$UPQ_CREDENTIALS
ENV UPQ_JOBS=$UPQ_JOBS
ENV UPQ_DIR=/upq

RUN mkdir -p $RAPID_FILES && chown -R www-data:www-data $RAPID_FILES \
 && mkdir -p $UPQ_JOBS && chown -R www-data:www-data $UPQ_JOBS

COPY --from=build /rapid-src/src/BuildGit /usr/local/bin
COPY --from=build /rapid-src/src/Streamer /usr/local/bin
ENV RAPID_STREAMER_BIN=/usr/local/bin/Streamer
COPY --from=build /spring-src/libunitsync.so /usr/local/lib
COPY  scripts/rapid-init.sh /usr/local/bin
COPY  scripts/update.sh /usr/local/bin/rapid-update-git.sh
COPY  scripts/update-repos.py /usr/local/bin/rapid-update-repos.py

RUN mkdir -p $RAPID_GIT $RAPID_PACKAGES \
 && touch "$RAPID_PACKAGES/repos" && gzip "$RAPID_PACKAGES/repos"

RUN rapid-init.sh \
 && rapid-update-repos.py \
 && rapid-update-git.sh \
 && chown -R www-data:www-data $RAPID_GIT \
 && chown -R www-data:www-data $RAPID_PACKAGES

COPY conf/apache.conf /etc/apache2/sites-available/rapid-packages.conf
COPY conf/upq.conf /etc/apache2/sites-available/upq.conf

COPY conf/ssl.crt /etc/apache2/ssl/ssl.crt
COPY conf/ssl.key /etc/apache2/ssl/ssl.key

RUN a2dissite 000-default && a2ensite rapid-packages && a2ensite upq \
 && a2enmod cgid && a2enmod ssl

RUN git clone --depth=1 https://github.com/badosu/upq /upq

COPY conf/upq.cfg /upq
COPY conf/config.php /upq/www
RUN bash -c "compgen -e | xargs -I @ sh -c 'printf "\"'s|\|@\||$@|g\n"'"' | sed -f /dev/stdin -i /upq/upq.cfg /upq/www/config.php"
