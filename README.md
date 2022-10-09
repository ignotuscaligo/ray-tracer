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

# Activate virtual environment

The Conan configuration for this project sets up the dependencies in a virtual environment:

## Windows

```
call activate.bat
```

## Linux / macOS

```
source activate.sh
```

# Configuring

Run `cmake` in the `build/` directory with the relative path to this project.

```
cmake ..
```

# Building

Run `cmake -- build .` in the `build/` directory. Build configuration can be selected with `--config Debug` or `--config Release`.

```
cmake --build . --target ray-tracer --config Release
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

# Testing

The `tests` target is configured along with the main executable above.

```
cmake --build . --target tests --config Release
```

## Windows

```
build\tests\Release\tests.exe -s -d yes
```

## Linux / macOS

```
build/tests/Release/tests -s -d yes
```
