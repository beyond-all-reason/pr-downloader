# syntax=docker/dockerfile:1
FROM debian:bullseye-slim AS build

RUN apt update
RUN apt install -y build-essential cmake git
RUN apt install -y libcurl4-openssl-dev libzip-dev liblua5.1-0-dev

WORKDIR /rapid-src

# RUN git clone https://github.com/spring/pr-downloader.git .

COPY cmake ./cmake
COPY src ./src
COPY test ./test
COPY ["CMakeLists.txt", "Doxyfile", "./"]

RUN git clone https://github.com/libgit2/libgit2.git src/lib/libgit2

RUN cmake -DRAPIDTOOLS=ON .
RUN make pr-downloader -j2
RUN make all

FROM php:apache-buster AS scratch

RUN apt update
RUN apt install -y git gzip python3
RUN apt install -y libcurl4-openssl-dev libzip-dev liblua5.1-0-dev
RUN apt install -y vim

ARG RAPID_GIT
ARG RAPID_PACKAGES
ARG RAPID_GIT_REPOS

ENV RAPID_GIT=$RAPID_GIT
ENV RAPID_PACKAGES=$RAPID_PACKAGES
ENV RAPID_GIT_REPOS=$RAPID_GIT_REPOS

COPY --from=build /rapid-src/src/BuildGit /usr/local/bin
COPY --from=build /rapid-src/src/BuildGit /usr/local/bin
COPY --from=build /rapid-src/src/Streamer /usr/local/bin
COPY  scripts/rapid-init.sh /usr/local/bin
COPY  scripts/update.sh /usr/local/bin/rapid-update-git.sh
COPY  scripts/update-repos.py /usr/local/bin/rapid-update-repos.py

RUN mkdir -p $RAPID_GIT $RAPID_PACKAGES \
 && touch "$RAPID_PACKAGES/repos" && gzip "$RAPID_PACKAGES/repos"

RUN mkdir /etc/sysconfig \
 && printf "RAPID_PACKAGES=$RAPID_PACKAGES\nexport RAPID_PACKAGES" >> /etc/apache2/envvars

RUN rapid-init.sh
RUN rapid-update-repos.py
RUN rapid-update-git.sh

RUN git clone https://github.com/spring/upq /upq

COPY conf/apache.conf /etc/apache2/sites-available/rapid-packages.conf
COPY conf/upq.conf /etc/apache2/sites-available/upq.conf
RUN a2dissite 000-default && a2ensite rapid-packages && a2ensite upq
