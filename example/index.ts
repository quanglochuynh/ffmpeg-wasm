import createModule from "@flinthuynh/ffmpeg-wasm";
import { readFile } from "node:fs/promises";

const wasm = await createModule();

console.log(wasm.add(2, 3)); // 5
console.log(wasm.multiply(2, 3)); // 6

const video = await readFile(new URL("../test-data/IMG_5255.MOV", import.meta.url));
if (!wasm.printVideoMetadata(video)) {
  throw new Error("Failed to read metadata from the sample video");
}