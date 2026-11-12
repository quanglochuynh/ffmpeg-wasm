# @locqhuynh/ffmpeg-wasm

A C++ WebAssembly package built with Emscripten. It exports math helpers and a
`Video` class that reads metadata and frames with FFmpeg.

## Installation

```sh
npm install @locqhuynh/ffmpeg-wasm
```

## Quickstart

Requirements: Node.js, pnpm, Git, and Emscripten (`emsdk`).

The build script expects `emsdk` in a sibling directory named `../emsdk`.
From the repository root, install and activate it once:

```sh
git clone https://github.com/emscripten-core/emsdk.git ../emsdk
(cd ../emsdk && ./emsdk install latest && ./emsdk activate latest)
```

Build FFmpeg for WebAssembly once (clones FFmpeg into `third_party/` and
installs a minimal build into `build/ffmpeg`; takes a few minutes):

```sh
bash scripts/build-ffmpeg.sh
```

Install dependencies and build the package:

```sh
pnpm install
pnpm build
```

Run the Node.js regression tests:

```sh
pnpm --dir example install
pnpm --dir example test
```

The output is written to `dist/`. To use the package from Node.js:

```js
import { readFile, writeFile } from "node:fs/promises";
import createModule from "@locqhuynh/ffmpeg-wasm";

const wasm = await createModule();
console.log(wasm.add(2, 3)); // 5
const bytes = await readFile("test-data/IMG_5255.MOV");
const video = new wasm.Video(bytes); // parses the metadata, throws on invalid data
console.log(video.width, video.height, video.duration);
video.printMetadata();
const jpeg = video.getFrame(10); // Uint8Array with the JPEG file bytes of frame 10
await writeFile("frame-10.jpg", jpeg);
video.delete(); // free the C++ object when done
```

`new Video(bytes)` accepts a `Uint8Array` (including a Node.js `Buffer`) and
reads the metadata with FFmpeg and exposes `container`, `fileSize`, `duration`, `videoCodec`, `width`, `height`,
and `frameRate`. `getFrame(index)` decodes the frame at that 0-based
index (display order) with FFmpeg and returns the bytes of a JPEG image; it
throws an `Error` if the index is out of range.

## Cloudflare Workers

The `@locqhuynh/ffmpeg-wasm/cloudflare` entry point bundles a precompiled
WebAssembly module and avoids dynamic JavaScript generation. It returns the
same API as the default entry point. The Worker bundler must support `.wasm`
imports as `WebAssembly.Module`, as the Cloudflare Vite plugin does.

The example is a Worker that accepts raw video bytes in a POST request. Start
it locally after building the package and installing the example dependencies:

```sh
pnpm build
pnpm --dir example install
pnpm --dir example dev
```

Send a video for JSON metadata, or add `?frame=1` for a JPEG:

```sh
curl --data-binary @test-data/IMG_5255.MOV http://localhost:5173/
curl --data-binary @test-data/IMG_5255.MOV 'http://localhost:5173/?frame=1' -o frame.jpg
```

The example limits uploads to 20 MiB. It buffers the input and the C++ `Video`
keeps another copy, so even smaller videos may exceed Workers' 128 MB isolate
memory limit. Frame extraction is CPU-intensive and decodes from the start on
each call. Test your own inputs against the [Workers limits](https://developers.cloudflare.com/workers/platform/limits/);
for larger workloads, consider Cloudflare Containers with native FFmpeg.

The current C++ error path can leak memory when video initialization fails.
Do not expose the example to untrusted uploads until that is fixed.

## Publishing

```bash
pnpm publish
```

Before publishing, review and satisfy FFmpeg's redistribution requirements.
The WebAssembly binary statically links FFmpeg under LGPL-2.1-or-later; in
particular, the release must include the applicable license notices and provide
the required source and means to relink with a modified FFmpeg. The current
package does not yet include those materials. See
[FFmpeg's license information](https://ffmpeg.org/legal.html).

## License

This project's own code is licensed under the [MIT License](./LICENSE).
The distributed WebAssembly binary also contains FFmpeg, which is licensed
under LGPL-2.1-or-later and includes components under other permissive licenses.
