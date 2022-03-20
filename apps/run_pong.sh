#!/bin/bash
docker build --progress plain -t dcastro/app-pong:latest . -f docker/Dockerfile.pong.run
docker run -it --rm --net=host -p 12345:12354 dcastro/app-pong:latest
