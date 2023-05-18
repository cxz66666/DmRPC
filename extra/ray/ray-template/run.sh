#!/usr/bin/env bash

#Cause the script to exit if a single command fails.
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" || exit; pwd)"

bazel --nosystem_rc --nohome_rc build //:example

ssh cxz@192.168.189.8 "rm -rf /home/cxz/.cache/bazel/_bazel_cxz/43592cfd1a9ce2b2ca84e2bfa46865d2/execroot/__main__/bazel-out/k8-fastbuild/bin/*"

scp "${ROOT_DIR}"/bazel-bin/example.so  cxz@192.168.189.8:/home/cxz/.cache/bazel/_bazel_cxz/43592cfd1a9ce2b2ca84e2bfa46865d2/execroot/__main__/bazel-out/k8-fastbuild/bin

"${ROOT_DIR}"/bazel-bin/example $(cat config)

