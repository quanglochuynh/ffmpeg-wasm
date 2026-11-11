import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import createModule from "@flinthuynh/ffmpeg-wasm";

test("reads metadata from a byte view and rejects invalid input", async () => {
  const output = [];
  const errors = [];
  const wasm = await createModule({
    print: (line) => output.push(line),
    printErr: (line) => errors.push(line),
  });
  const video = await readFile(new URL("../test-data/IMG_5255.MOV", import.meta.url));
  const padded = Buffer.concat([Buffer.from([1, 2]), video, Buffer.from([3])]);

  assert.equal(wasm.printVideoMetadata(padded.subarray(2, -1)), true);
  assert.ok(output.includes(`File size: ${video.length} bytes`));
  assert.ok(output.includes("Video codec: H.264 (avc1)"));
  assert.ok(output.includes("Resolution: 1080 x 1920"));
  assert.ok(output.includes("Duration: 3.433 seconds"));

  assert.equal(wasm.printVideoMetadata(new Uint8Array()), false);
  assert.equal(wasm.printVideoMetadata(new Uint8Array([0, 0, 0, 1])), false);
  assert.ok(errors.some((line) => line.includes("Video data is empty")));
  assert.ok(errors.some((line) => line.includes("Unable to parse video metadata")));
});
