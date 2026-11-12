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

Run the example app to build the package and read the test video from disk:

```sh
pnpm --dir example install
pnpm --dir example test
```

The output is written to `dist/`. To use the package from JavaScript:

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
