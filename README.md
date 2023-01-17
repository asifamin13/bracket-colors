# bracket-colors

Proof of concept for coloring nested brackets in Geany

## Building

### CMake

**Compiling**

```shell
$ mkdir build
$ cd build
$ cmake ../
$ make
```

Plugin is in `build/src/bracket-colors.so`

**Installing**

Install into existing geany lib directory with `make install` in the build directory or manually copy/symlink `bracket-colors.so`
