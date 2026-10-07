# Bajji image proxy

This Worker accepts `/cover` and `/fit` for StopWatch animated WebP, and
`/passport-cover` and `/passport-fit` for AI Passport static JPEG (120x160, quality 80).
It fetches the fixed UAPI image endpoint and passes its bytes to the Images binding;
arbitrary proxy origins and transformation parameters are not accepted.

The Passport routes are new in this branch and require deployment before firmware random
wallpaper downloads can use them. The implementation task has not deployed this Worker.

```sh
node --test worker.test.mjs
npx wrangler login
npx wrangler deploy
```

The production deployment used by the firmware is:

`https://bajji-image-proxy.eric3u.cc`
