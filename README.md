# @flinthuynh/ffmpeg-wasm

A C++ WebAssembly package built with Emscripten. It exports math helpers and a
lightweight MOV/MP4 metadata reader.

## Quickstart

Requirements: Node.js, pnpm, and Emscripten (`emsdk`).

The build script expects `emsdk` in a sibling directory named `../emsdk`.
From the repository root, install and activate it once:

```sh
git clone https://github.com/emscripten-core/emsdk.git ../emsdk
(cd ../emsdk && ./emsdk install latest && ./emsdk activate latest)
```

Install dependencies and build the package:

```sh
pnpm install
pnpm build
```

Run the example app to build the package and read the test video from disk:

```sh
pnpm --dir example install
pnpm --dir example test
```

The output is written to `dist/`. To use the package from JavaScript:

```js
import { readFile } from "node:fs/promises";
import createModule from "@flinthuynh/ffmpeg-wasm";

const wasm = await createModule();
console.log(wasm.add(2, 3)); // 5
const video = await readFile("test-data/IMG_5255.MOV");
if (!wasm.printVideoMetadata(video)) {
  throw new Error("Unable to read video metadata");
}
```

`printVideoMetadata` accepts a `Uint8Array` (including a Node.js `Buffer`) and
prints its container, duration, file size, video codec,
resolution, and average frame rate. The lightweight parser supports ISO
Base Media containers such as MOV/MP4; it is not a replacement for FFmpeg's
full demuxer.
