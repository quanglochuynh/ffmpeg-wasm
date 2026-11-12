import type { WasmModule } from "./index.js";

declare function createCloudflareModule(): Promise<WasmModule>;

export default createCloudflareModule;
