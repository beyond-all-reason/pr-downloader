Debian package build for Rapid Tools
====================================

This directory contains definition of
- Debian package for rapidtools
- docker file that prepared build environment

and is intented to be used from CI script like GitHub Action to build package with rapidtools.

Example manual usage looks like this:

```
docker build -f scripts/rapidtools-deb/buildenv.Dockerfile -t rapidtoolsbuild scripts/rapidtools-deb
docker run -v $(pwd)/:/src --rm rapidtoolsbuild /src/scripts/rapidtools-deb/build-deb.sh
```

and produces a bunch of deb packages in scripts/rapidtools-deb directory.

TODO: Would be good to see if all of that can be replaced by CPack once cmake files will look reasonable again.
