#!/usr/bin/env bash
# Entwicklungs-Workflow für OpenHU im Container.
#
#   ./dev.sh build            inkrementell bauen (nur geänderte Dateien) -> output/*.deb -> openhu:run neu
#   ./dev.sh build --aasdk    zusätzlich aasdk neu bauen (../aasdk-ref bzw. $AASDK_DIR, nach aasdk-Änderungen)
#   ./dev.sh image            Build-Image openhu:build komplett neu bauen (nach Dockerfile-/Abhängigkeitsänderungen)
#   ./dev.sh shell            Shell in der Build-Umgebung (Quellen eingehängt)
#   ./dev.sh reconnect        Handy per USB-Reset neu verbinden (laufendes ./run.sh)
#
# Danach wie gewohnt: ./run.sh
set -euo pipefail

DOCKER_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(dirname "$DOCKER_DIR")"
# aasdk source (default: next to this repository)
AASDK_DIR="${AASDK_DIR:-$(dirname "$REPO")/aasdk-ref}"
BUILD_IMAGE="openhu:build"
RUN_IMAGE="openhu:run"

# Build-Verzeichnisse liegen in Docker-Volumes, damit inkrementelle Builds schnell sind
# und keine root-Dateien im Quellbaum landen.
dev_container() {
  docker run --rm ${DEV_TTY:-} \
    -v "$REPO:/src" \
    -v "$AASDK_DIR:/aasdk" \
    -v "$DOCKER_DIR/output:/output" \
    -v openhu-build:/build/openhu \
    -v openhu-aasdk-build:/build/aasdk \
    -w /src \
    "$BUILD_IMAGE" "$@"
}

build_run_image() {
  docker build -f "$DOCKER_DIR/Dockerfile.run" -t "$RUN_IMAGE" "$DOCKER_DIR"
}

cmd_build() {
  local with_aasdk=false
  [[ "${1:-}" == "--aasdk" ]] && with_aasdk=true

  mkdir -p "$DOCKER_DIR/output"
  rm -f "$DOCKER_DIR"/output/*.deb

  dev_container bash -euo pipefail -c "
    if $with_aasdk; then
      echo '=== aasdk ==='
      cd /aasdk
      if [ -f /build/aasdk/build.ninja ]; then
        cmake --build /build/aasdk
      else
        BUILD_DIR=/build/aasdk BUILD_TESTS=OFF bash ./build.sh release --skip-protobuf --skip-absl
      fi
      rm -rf /output/aasdk-root
      DESTDIR=/output/aasdk-root cmake --install /build/aasdk >/dev/null
      cd /src
    fi
    # aasdk from the last --aasdk build instead of the one in the image
    if [ -d /output/aasdk-root ]; then
      # remove the image's aasdk first, ldconfig would otherwise keep linking the old library
      rm -f /usr/local/lib/libaasdk.so* /usr/local/lib/libaap_protobuf.so*
      cp -a /output/aasdk-root/. /
      ldconfig
    fi

    echo '=== openhu ==='
    cmake -S /src -B /build/openhu -DCMAKE_BUILD_TYPE=Release \
          -DCPACK_PROJECT_CONFIG_FILE=/src/cmake_modules/CPackProjectConfig.cmake >/dev/null
    cmake --build /build/openhu -j\$(nproc)
    touch /tmp/cpack-stamp
    cd /build/openhu && cpack -G DEB >/dev/null
    find /build/openhu -maxdepth 1 -name '*.deb' -newer /tmp/cpack-stamp -exec cp -v {} /output/ \;
    chown -R $(id -u):$(id -g) /output
  "

  build_run_image
}

cmd_image() {
  docker build -t "$BUILD_IMAGE" --build-context aasdk="$AASDK_DIR" "$REPO"
  rm -rf "$DOCKER_DIR/output/aasdk-root"
  mkdir -p "$DOCKER_DIR/output"
  docker run --rm -v "$DOCKER_DIR/output:/host-output" "$BUILD_IMAGE" \
    bash -c "rm -f /host-output/*.deb && cp -v /output/*.deb /host-output/ && chown -R $(id -u):$(id -g) /host-output"
  build_run_image
}

cmd_shell() {
  DEV_TTY=-it dev_container bash
}

cmd_reconnect() {
  docker exec openhu python3 -c '
import asyncio, json, websockets
async def main():
    async with websockets.connect("ws://127.0.0.1:5520") as ws:
        await ws.send(json.dumps({"type": "request", "id": 1, "method": "projection.reconnect"}))
        print(json.loads(await ws.recv()).get("result"))
asyncio.run(main())'
}

case "${1:-}" in
  build) shift; cmd_build "$@" ;;
  image) cmd_image ;;
  shell) cmd_shell ;;
  reconnect) cmd_reconnect ;;
  *) sed -n '2,11p' "$0"; exit 1 ;;
esac
