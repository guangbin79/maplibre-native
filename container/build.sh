#!/bin/bash

if [ $# != 1 ]
then
    echo "e.g.: $0 <linux-arm64-gl | linux-arm64-gles | linux-x86_64-gl | linux-x86_64-gles | all>"
    exit 1
fi

PROJ_DIR="$( cd "$( dirname "$0"   )" && pwd   )"
cd "${PROJ_DIR}" || exit 1
USER_ID=$(id -u)
GROUP_ID=$(id -g)

if [[ $1 == 'linux-x86_64-gl' || $1 == 'all' ]]
then
    sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose run qt_x86_64-compiler "${HOME}/container/linux-x86_64-gl"
fi

if [[ $1 == 'linux-x86_64-gles' || $1 == 'all' ]]
then
    sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose run qt_x86_64-compiler "${HOME}/container/linux-x86_64-gles"
fi

if [[ $1 == 'linux-arm64-gl' || $1 == 'all' ]]
then
    sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose run qt_arm64-compiler "${HOME}/container/linux-arm64-gl"
fi

if [[ $1 == 'linux-arm64-gles' || $1 == 'all' ]]
then
    sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose run qt_arm64-compiler "${HOME}/container/linux-arm64-gles"
fi

sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose down
