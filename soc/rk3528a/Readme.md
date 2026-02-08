After running build.bat, you have to run this command for the rk3528a

## Command
```bash
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=radxa-rock2a image
```
