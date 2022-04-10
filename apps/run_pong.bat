docker network inspect demonet > demonet.txt
find /c "demonet" demonet.txt >NUL
if %errorlevel% equ 1 docker network create --driver overlay --attachable --subnet 192.168.123.0/24 demonet
rm demonet.txt

docker build --progress plain -t dcastro/app-pong:latest . -f docker/Dockerfile.pong.run
docker run -it --rm --net demonet --ip 192.168.123.2 dcastro/app-pong
