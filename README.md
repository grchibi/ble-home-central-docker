# ble-home-central-docker

## Compile blescan

Before the compilation, create `config.yaml.1` and `config.yaml.2` from config.yaml.
In the build process these config files are needed and copied to docker images.
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
$ docker logs ble-home-central-docker_central-1_1 -f
$ docker logs ble-home-central-docker_central-2_1 -f
```
