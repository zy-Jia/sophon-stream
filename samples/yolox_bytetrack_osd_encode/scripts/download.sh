#!/bin/bash
pip3 install dfss

scripts_dir=$(dirname $(readlink -f "$0"))

pushd $scripts_dir

mkdir -p ../data

python3 -m dfss --url=open@sophgo.com:/sophon-stream/yolox_bytetrack_osd_encode/videos.tar
tar xvf videos.tar -C ../data
rm -rf videos.tar

mkdir -p ../data
python3 -m dfss --url=open@sophgo.com:/sophon-stream/yolox_bytetrack_osd_encode/models.tar
tar xvf models.tar -C ../data
rm -rf models.tar

python3 -m dfss --url=open@sophgo.com:/sophon-stream/yolox_bytetrack_osd_encode/coco.names
mv ./coco.names ../data

popd