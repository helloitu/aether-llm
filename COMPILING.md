# Compiling

Builds use the local Docker image `ps4-llm-toolchain:latest`.

## Build the Toolchain Image

Run this only when the image does not exist or `Dockerfile.build` changes:

```powershell
cd aether-llm
docker build -t ps4-llm-toolchain:latest -f Dockerfile.build .
```

## Compile the Homebrew

```powershell
cd aether-llm
docker run --rm -w /workspace -v "${PWD}:/workspace" ps4-llm-toolchain:latest bash -lc "make -j8"
```

Outputs:

- `Aether-v0.89.pkg`
- `eboot.bin`
- `build/source.elf`
- `build/source.oelf`

## Validate the PKG

```powershell
docker run --rm -w /workspace -v "${PWD}:/workspace" ps4-llm-toolchain:latest bash -lc '"$OO_PS4_TOOLCHAIN/bin/linux/PkgTool.Core" pkg_validate Aether-v0.89.pkg'
```

## Clean

```powershell
docker run --rm -w /workspace -v "${PWD}:/workspace" ps4-llm-toolchain:latest bash -lc "make clean"
```

`make clean` removes generated build outputs and packages. It does not remove source files, package assets, external sources, or the Web UI.

The default package category is `gd` so the app launches as a big game app and the PS4 Share menu can record it. If a UI regression needs the old mini-app launch type, build with `make APP_CATEGORY=gde -j8`.

## Package Contents

`pkg.gp4` packages:

- `eboot.bin`
- `sce_sys/icon0.png`
- `sce_sys/pic1.png`
- `sce_sys/param.sfo`
- `Media/jb.prx`
- `web/index.html`
