docker network inspect demonet > demonet.txt
find /c "demonet" demonet.txt >NUL
if %errorlevel% equ 1 docker network create --driver overlay --attachable --subnet 192.168.123.0/24 demonet
rm demonet.txt

docker build --progress plain -t dcastro/app-peer . -f docker/Dockerfile.peer.run
@REM TODO: detach in an extern console
docker run --name node0 -d --rm --net demonet --ip 192.168.123.2 dcastro/app-peer /root/peer 12345
docker run --name node1 -d --rm --net demonet --ip 192.168.123.3 dcastro/app-peer /root/peer 12345 --peer 192.168.123.2 12345
docker run --name node2 -d --rm --net demonet --ip 192.168.123.4 dcastro/app-peer /root/peer 12345 --peer 192.168.123.3 12345
