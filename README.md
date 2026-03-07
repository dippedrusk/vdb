# VDB

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
