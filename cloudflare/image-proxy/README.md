# Bajji image proxy

This Worker accepts only `/cover` and `/fit` requests, fetches the fixed UAPI image endpoint,
and passes its bytes to the Images binding. It returns an animated WebP that the device can
decode; arbitrary proxy origins and transformation parameters are not accepted.

```sh
node --test worker.test.mjs
npx wrangler login
npx wrangler deploy
```

The production deployment used by the firmware is:

`https://bajji-image-proxy.eric3u.cc`
