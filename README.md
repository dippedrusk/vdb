# VDB

Debugger I'm building by following [Sy Brand](https://github.com/TartanLlama)'s book [Building a Debugger](https://nostarch.com/building-a-debugger).

## Dependencies
Run this outside this repo:

```
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
```

## Building
After cloning, run

```
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../../vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build .
```

Change path to vcpkg as needed.

## Running the debugger
From the `build/` subdirectory, run
```
./tools/vdb
```

## Running tests
From the `build/tests` subdirectory, run
```
./tests
```

## Helpful aliases
For people like me who obstinately refuse to use anything but vim, add this to your `~/.bashrc` or equivalent
```
alias btvdb="pushd ~/vdb/build; cmake --build . && cd test/ && ./tests; popd;"
```
