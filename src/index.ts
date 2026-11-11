export interface WasmModule {
  add(a: number, b: number): number;
  multiply(a: number, b: number): number;
  printVideoMetadata(bytes: Uint8Array): boolean;
}

declare function createModule(): Promise<WasmModule>;

export default createModule;