import assert from "node:assert/strict";
import { readFile, readdir } from "node:fs/promises";
import { test } from "node:test";
import { Miniflare, convertV4MiniflareOptions } from "miniflare";

test("Worker reads video bytes and returns metadata or a JPEG", async () => {
  const bundle = ".cloudflare/output/v0/workers/default/bundle";
  const [wasmFile] = (await readdir(`${bundle}/assets`)).filter((file) => file.endsWith(".wasm"));
  assert.ok(wasmFile, "Worker bundle must contain a WASM module");
  const runtime = new Miniflare(convertV4MiniflareOptions({
    modules: [
      { type: "ESModule", path: "index.js", contents: await readFile(`${bundle}/index.js`, "utf8") },
      { type: "CompiledWasm", path: `assets/${wasmFile}`, contents: await readFile(`${bundle}/assets/${wasmFile}`) },
    ],
    compatibilityDate: "2026-11-12",
  }));
  try {
    const bytes = await readFile(new URL("../test-data/IMG_5255.MOV", import.meta.url));
    const metadata = await runtime.dispatchFetch("http://localhost/", { method: "POST", body: bytes });
    assert.equal(metadata.status, 200);
    const data = await metadata.json();
    assert.equal(data.videoCodec, "h264");
    assert.equal(data.width, 1080);
    assert.equal(data.height, 1920);
    assert.equal(data.fileSize, bytes.length);

    const frame = await runtime.dispatchFetch("http://localhost/?frame=1", { method: "POST", body: bytes });
    assert.equal(frame.status, 200);
    assert.equal(frame.headers.get("content-type"), "image/jpeg");
    const jpeg = new Uint8Array(await frame.arrayBuffer());
    assert.deepEqual([...jpeg.subarray(0, 2)], [0xff, 0xd8]);
    assert.deepEqual([...jpeg.subarray(-2)], [0xff, 0xd9]);

    const method = await runtime.dispatchFetch("http://localhost/");
    assert.equal(method.status, 405);
    const badFrame = await runtime.dispatchFetch("http://localhost/?frame=-1", { method: "POST" });
    assert.equal(badFrame.status, 400);
    const oversized = await runtime.dispatchFetch("http://localhost/", {
      method: "POST",
      body: new Uint8Array(20 * 1024 * 1024 + 1),
    });
    assert.equal(oversized.status, 413);
  } finally {
    await runtime.dispose();
  }
});
