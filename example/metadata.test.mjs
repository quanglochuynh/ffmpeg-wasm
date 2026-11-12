import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import createModule from "@flinthuynh/ffmpeg-wasm";

test("Video parses metadata from bytes and rejects invalid input", async () => {
  const output = [];
  const wasm = await createModule({ print: (line) => output.push(line) });
  const file = await readFile(new URL("../test-data/IMG_5255.MOV", import.meta.url));
  const padded = Buffer.concat([Buffer.from([1, 2]), file, Buffer.from([3])]);

  const video = new wasm.Video(padded.subarray(2, -1));
  assert.equal(video.container, "QuickTime / MOV");
  assert.equal(video.fileSize, file.length);
  assert.equal(video.videoCodec, "h264");
  assert.equal(video.width, 1080);
  assert.equal(video.height, 1920);
  assert.equal(video.duration.toFixed(3), "3.433");
  assert.equal(video.frameRate.toFixed(0), "30");

  video.printMetadata();
  assert.ok(output.includes("Resolution: 1080 x 1920"));

  const first = video.getFrame(0);
  const last = video.getFrame(102);
  for (const frame of [first, last]) {
    assert.ok(frame instanceof Uint8Array);
    assert.deepEqual([...frame.subarray(0, 2)], [0xff, 0xd8]); // JPEG start
    assert.deepEqual([...frame.subarray(-2)], [0xff, 0xd9]); // JPEG end
  }
  assert.notDeepEqual(first, last);
  assert.throws(() => video.getFrame(1000), /out of range/);
  assert.throws(() => video.getFrame(-1), /must not be negative/);
  video.delete();

  assert.throws(() => new wasm.Video(new Uint8Array()), /Video data is empty/);
  assert.throws(() => new wasm.Video(new Uint8Array([0, 0, 0, 1])), /Unable to open the video/);
});
