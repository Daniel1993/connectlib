docker build --progress plain -t dcastro/connlib-deb:latest . -f docker/Dockerfile.deb
docker run -it --rm dcastro/connlib-deb:latest
