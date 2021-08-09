#!/bin/bash

cd $RAPID_GIT

for repo in ${RAPID_GIT_REPOS//,/ }
do
  git clone $repo
  mkdir $RAPID_PACKAGES/${repo##*/}
done

