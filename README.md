# Voice

## Build locally

```
sudo docker build -t alliedmodders-dev:local . -f Dockerfile.dev
sudo docker run -it --rm --entrypoint /bin/bash -v /path/to/dependencies:/alliedmodders -v /path/to/extension:/alliedmodders/extension alliedmodders-dev:local
cd /alliedmodders/extension/ && mkdir -p build && cd build && python3 ../configure.py --enable-optimize --targets=x86_64 --sdks=cs2 && ambuild
```

## Features

- [x] Metamod implementation
- Platform support
  - [ ] Windows
  - [x] Linux
- [x] Detour system
- [x] Gamedata support
  - [x] Signature
  - [ ] Offset
  - [ ] Patch
- [x] Protobuf support
- Voice
  - [x] PlayerSpeaking
  - [x] PlayerSpeakingStart
  - [x] PlayerSpeakingEnd
  - [x] TCP socket to send voice
  - [ ] Celt codec support
  - [ ] Send voice
- Chat
  - [x] Hook Host_Say
  - [x] SendConsoleChat
- [ ] CVar support

