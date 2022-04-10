#!/bin/bash
docker network inspect demonet > demonet.txt
if [[ -z $(grep "demonet" demonet.txt) ]]
then
	docker network create --driver overlay --attachable --subnet 192.168.123.0/24 demonet
fi
rm demonet.txt

docker build --progress plain -t dcastro/app-pong:latest . -f docker/Dockerfile.pong.run
docker run -it --rm --net demonet --ip 192.168.123.2 dcastro/app-pong
