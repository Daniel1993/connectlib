#!/bin/bash
docker build --progress plain -t dcastro/app-ping:latest . -f docker/Dockerfile.ping.run
docker run -it --rm --net=host dcastro/app-ping:latest
