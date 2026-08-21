const variants = {
  cover: {
    transform: { width: 466, height: 466, fit: "cover", sharpen: 1 },
    quality: 90,
  },
  fit: {
    transform: { width: 328, height: 328, fit: "contain", sharpen: 1 },
    quality: 85,
  },
};

const valuePattern = /^[a-z0-9][a-z0-9_]{0,31}$/;
const noncePattern = /^\d{1,10}$/;
const imageTypePattern = /^image\/(?:jpeg|png|gif|webp)(?:;|$)/i;

function error(message, status = 400) {
  return new Response(message, {
    status,
    headers: { "content-type": "text/plain; charset=utf-8" },
  });
}

export default {
  async fetch(request, env) {
    if (request.method !== "GET") return error("Method not allowed", 405);

    const requestUrl = new URL(request.url);
    const mode = requestUrl.pathname.slice(1);
    const variant = variants[mode];
    if (!variant) return error("Not found", 404);

    const allowed = new Set(["category", "type", "_"]);
    for (const key of requestUrl.searchParams.keys()) {
      if (!allowed.has(key) || requestUrl.searchParams.getAll(key).length !== 1) {
        return error("Invalid query");
      }
    }

    const category = requestUrl.searchParams.get("category") ?? "";
    const type = requestUrl.searchParams.get("type") ?? "";
    const nonce = requestUrl.searchParams.get("_") ?? "";
    if ((!category && type) || (category && !valuePattern.test(category)) ||
        (type && !valuePattern.test(type)) || !noncePattern.test(nonce) ||
        BigInt(nonce) > 0xffffffffn) {
      return error("Invalid query");
    }

    const source = new URL("https://uapis.cn/api/v1/random/image");
    if (category) source.searchParams.set("category", category);
    if (type) source.searchParams.set("type", type);
    source.searchParams.set("_", nonce);

    // UAPI's random sources can fail or exceed the Images binding's 20 MB input limit.
    // A retry query rolls a fresh source instead of replaying the rejected response.
    for (let attempt = 0; attempt < 3; ++attempt) {
      const candidate = new URL(source);
      if (attempt) candidate.searchParams.set("worker_retry", String(attempt));
      const response = await fetch(new Request(candidate, {
        headers: { accept: "image/webp,image/gif,image/png,image/jpeg" },
      }));
      if (!response.ok || !imageTypePattern.test(response.headers.get("content-type") ?? "")) {
        continue;
      }
      try {
        const result = await env.IMAGES.input(response.body)
          .transform(variant.transform)
          .output({ format: "image/webp", quality: variant.quality, anim: true });
        const transformed = await result.response();
        const headers = new Headers(transformed.headers);
        headers.set("cache-control", "public, max-age=31536000, immutable");
        return new Response(transformed.body, { status: transformed.status, headers });
      } catch {
        continue;
      }
    }
    return error("Source unavailable", 502);
  },
};
