#!/bin/bash

if [ $# != 1 ]
then
    echo "e.g.: $0 <linux-arm64 | linux-arm64-egl | linux-x86_64 | linux-x86_64-egl | windows-x86_64 | all>"
    exit 1
fi

PROJ_DIR="$( cd "$( dirname "$0"   )" && pwd   )"
cd "${PROJ_DIR}" || exit 1
USER_ID=$(id -u)
GROUP_ID=$(id -g)

if [[ $1 == 'linux-x86_64' || $1 == 'all' ]]
then
    sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose run qt_x86_64-compiler "${HOME}/container/linux-x86_64"
fi

if [[ $1 == 'linux-x86_64-egl' || $1 == 'all' ]]
then
    sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose run qt_x86_64-compiler "${HOME}/container/linux-x86_64-egl"
fi

if [[ $1 == 'linux-arm64' || $1 == 'all' ]]
then
    sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose run qt_arm64-compiler "${HOME}/container/linux-arm64"
fi

if [[ $1 == 'linux-arm64-egl' || $1 == 'all' ]]
then
    sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose run qt_arm64-compiler "${HOME}/container/linux-arm64-egl"
fi

if [[ $1 == 'linux-mips64el-egl' || $1 == 'all' ]]
then
    sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose run qt_mips64el-compiler "${HOME}/container/linux-mips64el-egl"
fi

if [[ $1 == 'windows-x86_64' || $1 == 'all' ]]
then
    sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose run qt_win64-compiler "${HOME}/container/windows-x86_64"
fi

sudo UID="${USER_ID}" GID="${GROUP_ID}" HOME="${HOME}" docker-compose down
