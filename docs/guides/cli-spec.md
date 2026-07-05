---
title: "CLI 规格"
category: guide
status: 现行
owner: engine
created: 2026-01-02
last_updated: 2026-06-07
---

# CLI 规格

仓库构建 CLI 是 `gnb`。

## 构建

```bash
./gnb.sh build [target]
./gnb.bat build [target]
```

选项：`--clean`、`--reconfigure`、`--jobs N`、`--no-unity`、`--lto`、`--print-cmd`。

## 运行

```bash
./gnb.sh run [target] [-- app-args]
./gnb.bat run [target] [-- app-args]
```

选项：`--bin-dir`、`--present-mode`、`--scene`、`--list`、`--dry-run`。

## 初始化

```bash
./gnb.sh setup
./gnb.bat setup
```

选项：`--skip-paks`、`--vcpkg-only`、`--refresh`。

完整命令手册见 [gnb-cli.md](gnb-cli.md)。
