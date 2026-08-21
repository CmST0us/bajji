# vendor 依赖是固定版本 + 打补丁，不能直接改

## 机制

`device/vendor/**` **在 .gitignore 里**，是由 `device/tools/fetch_deps.py` 从两个纳入版本管理的文件重建出来的：

- `device/deps.lock.json` —— 每个依赖一条，含 `url`、`ref`、可选的 `submodules` 和可选的 `patch` 文件名
- `device/patches/*.patch` —— `git diff` 的输出，checkout 之后在 vendor 仓库里 apply

```python
run("git", "checkout", "--detach", "FETCH_HEAD", cwd=repo)
if patch_name := item.get("patch"):
    patch = ROOT / "patches" / str(patch_name)
    if not is_applied(repo, patch):
        run("git", "apply", "--check", str(patch), cwd=repo)
        run("git", "apply", str(patch), cwd=repo)
```

`is_applied()` 用的是 `git apply --reverse --check`，所以重复跑 `fetch_deps.py` 是幂等的。

## 后果

**改了 `device/vendor/**` 而不做别的，这个改动在顶层 `git status` 里看不见，并且会被下一次 `fetch_deps.py` 抹掉。**

合法用途只有两种。

一是**一次性插桩**：加日志、烧、读、然后在 vendor 仓库内部 `git checkout -- <文件>` 还原。除此之外没有别的办法撤销它。

二是**要留下来的改动**，那就必须打包：

```sh
cd device/vendor/<Name>
git diff > ../../patches/<Name>.patch
cd ../..
# 在 deps.lock.json 里给那个依赖加上 "patch": "<Name>.patch"
cd vendor/<Name> && git apply --reverse --check ../../patches/<Name>.patch   # 验证幂等
```

漏了第二步的话，本地一切正常、编译也过、review 时看着也没问题，然后在别人干净 checkout 或者下一次刷新依赖时悄无声息地消失。

## vendor 仓库要单独查

顶层 `git status` 对 vendor 的状态一无所知。宣布工作区干净之前，把你动过的 vendor 仓库都查一遍：

```sh
for d in device/vendor/*/; do echo "== $d"; git -C "$d" status --short; done
```

目前已有的补丁：`M5IOE1.patch`、`M5PM1.patch`、`lvgl.patch`、`BMI270_BMM150_Sensor.patch`。

## 别还原你没写的东西

vendor 仓库里会同时堆着不止一件事的改动。在里面执行 `git checkout` 之前，先把修改的文件列出来，只还原你自己那些。在 vendor 里来一发 `git checkout .` 会静默干掉别人还没打包的改动——[concurrent-agent-sessions.md](concurrent-agent-sessions.md) 里就有实例。
