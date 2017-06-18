#!/bin/bash

function tag()
{
	echo ""
	echo "*********************"
	echo "TAGGING"
	echo "*********************"
	ctags --fields=+iaS --extra=+q -R *
}

function compile_shaders()
{
	echo ""
	echo "*********************"
	echo "Compile shaders"
	echo "*********************"
	pushd /home/al/amvk/shader

	for filename in *.*; do
		echo $filename
		../glslangValidator -V $filename -o spv/$filename.spv
	done

	popd
}

make_clean=false; compile_shaders=true

# Transform long options to short ones
for arg in "$@"; do
  shift
  case "$arg" in
    "--clean") set -- "$@" "-c" ;;
    "--skip_shaders") set -- "$@" "-s" ;;
    *)        set -- "$@" "$arg"
  esac
done

while getopts "cs" opt 
do
	case "$opt" in
		"c") make_clean=true ;;
		"s") compile_shaders=false ;;
	esac
done



pushd /home/al/amvk

tag

pwd
if $compile_shaders; then
	compile_shaders
fi

if $make_clean
then
	echo "*********************"
	echo "Make clean"
	echo "*********************"

	make clean
	compile_shaders
	make
else
	echo "*********************"
	echo "Make"
	echo "*********************"
	make
fi

popd
