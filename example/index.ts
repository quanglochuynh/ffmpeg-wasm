import createModule from "@locqhuynh/ffmpeg-wasm/cloudflare";

const MAX_VIDEO_BYTES = 20 * 1024 * 1024;
const wasm = await createModule();

export default {
  async fetch(request: Request): Promise<Response> {
    if (request.method !== "POST") {
      return new Response("Send a video as the POST body", {
        status: 405,
        headers: { Allow: "POST" },
      });
    }

    const frameParam = new URL(request.url).searchParams.get("frame");
    if (frameParam !== null && (!/^\d+$/.test(frameParam) || Number(frameParam) > 2147483647)) {
      return new Response("Frame must be a non-negative integer", { status: 400 });
    }

    const length = request.headers.get("content-length");
    if (length !== null && (!/^\d+$/.test(length) || Number(length) > MAX_VIDEO_BYTES)) {
      return new Response("Video exceeds the 20 MiB limit", { status: 413 });
    }
    if (!request.body) {
      return new Response("Video body is required", { status: 400 });
    }

    const reader = request.body.getReader();
    const bytes = new Uint8Array(MAX_VIDEO_BYTES);
    let size = 0;
    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        if (value.length > MAX_VIDEO_BYTES - size) {
          await reader.cancel();
          return new Response("Video exceeds the 20 MiB limit", { status: 413 });
        }
        bytes.set(value, size);
        size += value.length;
      }
    } finally {
      reader.releaseLock();
    }

    const video = new wasm.Video(bytes.subarray(0, size));
    try {
      if (frameParam !== null) {
        const frame = video.getFrame(Number(frameParam));
        return new Response(new Uint8Array(frame).buffer, {
          headers: { "Content-Type": "image/jpeg" },
        });
      }
      return Response.json({
        container: video.container,
        fileSize: video.fileSize,
        duration: video.duration,
        videoCodec: video.videoCodec,
        width: video.width,
        height: video.height,
        frameRate: video.frameRate,
      });
    } finally {
      video.delete();
    }
  },
};
