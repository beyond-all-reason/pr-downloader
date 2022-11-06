ARG DISTRO=docker.io/library/debian:bullseye
FROM $DISTRO

RUN apt-get update \
 && apt-get upgrade -y \
 && apt-get install -y build-essential debhelper devscripts
COPY pkg/debian/control .
RUN mk-build-deps \
    -t 'apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends -y' \
    -i -r control
RUN mkdir /src
WORKDIR /src
