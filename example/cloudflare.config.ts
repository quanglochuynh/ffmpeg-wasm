import { defineConfig } from "cf/config";

export default defineConfig({
  worker: {
    name: "ffmpeg-wasm-example",
    entrypoint: "./index.ts",
    compatibilityDate: "2026-11-12",
    observability: {
      enabled: true,
      traces: { enabled: true },
    },
  },
});
