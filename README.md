# Initial setup

This repo uses submodules to manage dependencies, so be sure to initialize those:

```
git clone --recurse-submodules https://github.com/ignotuscaligo/ray-tracer.git
```

or

```
git clone https://github.com/ignotuscaligo/ray-tracer.git
cd ray-tracer
git submodule update --init
```

### Linux

Install libpng-dev

```
sudo apt install libpng-dev
```

# Configuring

```
mkdir build
cd build
cmake ..
```

# Building

```
cd build
cmake --build . --config Release
```

# Running

## Windows
```
build\Release\ray-tracer.exe test.json
```

## Linux / macOS
```
build/Release/ray-tracer test.json
```
