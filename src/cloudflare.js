import createModule from "./worker.js";
import wasmModule from "./worker.wasm";

export default function createCloudflareModule() {
  return createModule({
    instantiateWasm(imports, receiveInstance) {
      const instance = new WebAssembly.Instance(wasmModule, imports);
      receiveInstance(instance, wasmModule);
      return instance.exports;
    },
  });
}
