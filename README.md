# CONNECTLIB

In this library I implement a very simple interface to use POSIX C internet sockets (TCP and UDP).

## Usage

Run `run.sh` to execute the tests in docker.

There is a ping-pong application. Run `./apps/run_pong.sh` for the server that will listening to pings. Start the ping with `./apps/run_ping.sh`. You can change the location of the server in `docker/Dockerfile.ping.run`.
