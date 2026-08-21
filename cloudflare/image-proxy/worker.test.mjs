import assert from "node:assert/strict";
import test from "node:test";

import worker from "./worker.mjs";

test("builds fixed Cloudflare image transformations", async () => {
  const originalFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (request) => {
    calls.push({ request });
    return new Response("original", { headers: { "content-type": "image/jpeg" } });
  };
  const imageCalls = [];
  const env = {
    IMAGES: {
      input(stream) {
        imageCalls.push({ stream });
        return {
          transform(options) {
            imageCalls.at(-1).transform = options;
            return this;
          },
          async output(options) {
            imageCalls.at(-1).output = options;
            return {
              response() {
                return new Response("image", { headers: { "content-type": "image/webp" } });
              },
            };
          },
        };
      },
    },
  };

  try {
    const cover = await worker.fetch(new Request(
      "https://proxy.example/cover?category=bq&type=eciyuan&_=4294967295",
    ), env);
    const fit = await worker.fetch(new Request("https://proxy.example/fit?category=acg&_=7"), env);

    assert.equal(cover.status, 200);
    assert.equal(fit.status, 200);
    assert.equal(cover.headers.get("cache-control"), "public, max-age=31536000, immutable");
    assert.equal(calls[0].request.url,
      "https://uapis.cn/api/v1/random/image?category=bq&type=eciyuan&_=4294967295");
    assert.equal(imageCalls[0].transform.fit, "cover");
    assert.equal(imageCalls[0].output.quality, 90);
    assert.equal(imageCalls[0].output.format, "image/webp");
    assert.equal(imageCalls[0].output.anim, true);
    assert.equal(imageCalls[1].transform.fit, "contain");
    assert.equal(imageCalls[1].output.quality, 85);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("rejects requests outside the two fixed UAPI variants", async () => {
  const rejected = [
    new Request("https://proxy.example/other?_=1"),
    new Request("https://proxy.example/cover"),
    new Request("https://proxy.example/cover?url=https://example.com/image.jpg&_=1"),
    new Request("https://proxy.example/cover?category=../bad&_=1"),
    new Request("https://proxy.example/cover?type=pc&_=1"),
    new Request("https://proxy.example/cover?_=4294967296"),
    new Request("https://proxy.example/cover?_=1&_=2"),
    new Request("https://proxy.example/cover?_=1", { method: "POST" }),
  ];
  const statuses = [];
  for (const request of rejected) statuses.push((await worker.fetch(request, {})).status);
  assert.deepEqual(statuses, [404, 400, 400, 400, 400, 400, 400, 405]);
});

test("retries a flaky UAPI source before transforming", async () => {
  const originalFetch = globalThis.fetch;
  const sourceUrls = [];
  globalThis.fetch = async (request) => {
    sourceUrls.push(request.url);
    if (sourceUrls.length === 1) return new Response("upstream error", { status: 502 });
    return new Response("original", { headers: { "content-type": "image/gif" } });
  };
  let transforms = 0;
  const env = {
    IMAGES: {
      input() {
        return {
          transform() { return this; },
          async output() {
            if (++transforms === 1) throw new Error("image too large");
            return { response: () => new Response("image") };
          },
        };
      },
    },
  };

  try {
    const response = await worker.fetch(new Request(
      "https://proxy.example/cover?category=furry&type=4k&_=9",
    ), env);
    assert.equal(response.status, 200);
    assert.equal(sourceUrls.length, 3);
    assert.equal(transforms, 2);
    assert.match(sourceUrls[2], /worker_retry=2$/);
  } finally {
    globalThis.fetch = originalFetch;
  }
});
