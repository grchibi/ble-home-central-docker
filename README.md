# ble-home-central-docker

## Build blescan

Before building the software, edit config.yaml.
In the build process the config file is needed and copied to docker image.

Then, edit DOCKER/Dockerfile.CPL and DOCKER/Dockerfile to specify the base image.
- FROM arm32v6/alpine       => for Raspberry Pi Zero
- FROM arm32v7/alpine:3.12  => for Raspberry Pi 3, 4

Finish editing the Dockerfile, execute the following command.
```
$ docker build -t alpine-dev -f DOCKER/Dockerfile.CPL .

$ docker run --name cppdev -v "$PWD":/home -it alpine-dev
/ $ cd home; make build
```

## Register to Systemd

```
$ cp -a ble-bme280-central.service /etc/systemd/system/
$ systemctl enable ble-bme280-central
```

## Check the logs
```
$ docker logs ble-home-central-docker_central_1 -f
```
You can view the journal logs, too.
```
$ journalctl -u ble-bme280-central.service --no-pager --since="2023-01-27 18:00:00"
```
