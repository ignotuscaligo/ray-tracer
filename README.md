# Initial setup

## Install Conan

This project uses Conan to manage external dependencies. Install for your environment using their instructions at [Conan](https://docs.conan.io/en/latest/installation.html)

## Set up build directory

Create a new `build/` directory either in this folder, or externally. Keep track of the build folder path relative to this project.

```
mkdir build
cd build
```

# Installing dependencies

Run `conan install` in the `build/` directory with the relative path to this project.

```
conan install ..
```

# Configuring

Run `cmake` in the `build/` directory with the relative path to this project.

```
cmake ..
```

# Building

Run `cmake -- build .` in the `build/` directory. Build configuration can be selected with `--config Debug` or `--config Release`.

```
cmake --build . --config Release
```

# Running

## Windows
```
build\bin\ray-tracer.exe test.json
```

## Linux / macOS
```
build/Release/ray-tracer test.json
```
