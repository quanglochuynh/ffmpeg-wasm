export declare class Video {
  /** Parses the metadata from the file bytes; throws an Error if they are not a readable video. */
  constructor(bytes: Uint8Array);
  readonly container: string;
  /** Size in bytes. */
  readonly fileSize: number;
  /** Duration in seconds. */
  readonly duration: number;
  /** FFmpeg codec name, e.g. "h264" or "hevc". */
  readonly videoCodec: string;
  readonly width: number;
  readonly height: number;
  /** Average frames per second, 0 if unknown. */
  readonly frameRate: number;
  /**
   * Decodes the frame at `index` (0-based, display order) and returns the bytes
   * of a JPEG image file. Throws an Error if the index is out of range.
   */
  getFrame(index: number): Uint8Array;
  printMetadata(): void;
  /** Frees the underlying C++ object. */
  delete(): void;
}

export interface WasmModule {
  add(a: number, b: number): number;
  multiply(a: number, b: number): number;
  Video: typeof Video;
}

declare function createModule(moduleOverrides?: object): Promise<WasmModule>;

export default createModule;
