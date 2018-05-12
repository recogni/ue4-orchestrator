#!/bin/bash
echo "Set the environment variable UE4 in the console or in the system variables"
echo "For example in my Linux machine, I use 'export UE4=/home/unreal/UnrealEngine/4.18'"

unameOut="$(uname -s)"
case "${unameOut}" in
    Linux*)     machine=Linux;;
    Darwin*)    machine=Mac;;
esac
echo ${machine}

plugin_file=`pwd`/UE4Orchestrator.uplugin
package_folder=`pwd`/Plugins/UE4Orchestrator
"${UE4}"/Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -plugin=${plugin_file} -package=${package_folder} -rocket -targetplatforms=${machine}
