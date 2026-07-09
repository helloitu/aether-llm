# Compiling

Builds use the local Docker image `ps4-llm-toolchain:latest`.

## Build the Toolchain Image

Run this only when the image does not exist or `Dockerfile.build` changes:

```powershell
cd C:\Users\guigu\tt\aether-llm
docker build -t ps4-llm-toolchain:latest -f Dockerfile.build .
```

## Compile the Homebrew

```powershell
cd C:\Users\guigu\tt\aether-llm
docker run --rm -w /workspace -v "${PWD}:/workspace" ps4-llm-toolchain:latest bash -lc "make -j8"
```

Outputs:

- `Aether-v0.88.pkg`
- `eboot.bin`
- `build/source.elf`
- `build/source.oelf`

## Validate the PKG

```powershell
docker run --rm -w /workspace -v "${PWD}:/workspace" ps4-llm-toolchain:latest bash -lc '"$OO_PS4_TOOLCHAIN/bin/linux/PkgTool.Core" pkg_validate Aether-v0.88.pkg'
```

## Clean

```powershell
docker run --rm -w /workspace -v "${PWD}:/workspace" ps4-llm-toolchain:latest bash -lc "make clean"
```

`make clean` removes generated build outputs and packages. It does not remove source files, package assets, external sources, or the Web UI.

## Package Contents

`pkg.gp4` packages:

- `eboot.bin`
- `sce_sys/icon0.png`
- `sce_sys/pic1.png`
- `sce_sys/param.sfo`
- `Media/jb.prx`
- `web/index.html`
