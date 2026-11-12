import createModule from "@flinthuynh/ffmpeg-wasm";
import { readFile, writeFile } from "node:fs/promises";

const wasm = await createModule();

console.log(wasm.add(2, 3)); // 5
console.log(wasm.multiply(2, 3)); // 6

const bytes = await readFile(new URL("../test-data/video_20261107_155631.mp4", import.meta.url));
const video = new wasm.Video(bytes);
try {
  video.printMetadata();
  console.log(`${video.width}x${video.height}, ${video.duration}s`);

  const frame = video.getFrame(1);
  await writeFile(new URL("../frame-1.jpg", import.meta.url), frame);
  console.log(`Saved frame 1 (${frame.length} bytes) to frame-1.jpg`);
} finally {
  video.delete();
}
