# LRIPhoto

Tk extension that transforms photo images in place: scale, rotate, gray levels, black and white.
Package `LRIPhoto` 1.0. It builds against Tcl/Tk 8.5/8.6 and Tcl/Tk 9 from the same sources.

```tcl
package require LRIPhoto
LRI::imgtransform image ?options?        ;# transform in place
LRI::imgcopy srcImage destImage ?options? ;# copy, then transform the copy
```

| Option | Effect |
|---|---|
| `-scale %` | uniform scale, in percent |
| `-scalex %` / `-scaley %` | scale along one axis (then `-scale` is ignored) |
| `-rotate deg` | clockwise rotation, multiple of 90 |
| `-gray bool` | convert to gray levels |
| `-bw bool` | convert to black and white (Floyd–Steinberg dithering) |

## Build

TEA (autoconf) build, out of the source tree:

```sh
mkdir build-tcl8 && cd build-tcl8
../configure --with-tcl=/path/to/tcl8.6/lib --with-tk=/path/to/tk8.6/lib && make   # -> libLRIPhoto1.0.so
mkdir build-tcl9 && cd build-tcl9
../configure --with-tcl=/path/to/tcl9/lib --with-tk=/path/to/tk9/lib && make       # -> libtcl9LRIPhoto1.0.so
make install
```

On Windows, build with MinGW/MSYS2 the same way.
`configure` is already generated; run `autoconf` only after editing `configure.ac`.

## Tests

The tests need Tk, hence an X display:

```sh
cd build-tcl9 && make test
```

## License

GPL-3.0-or-later. Original code by Xavier Garreau (2005); Tcl 9 port and fixes
Copyright (c) Luc Moulinier. See [LICENSE](LICENSE).
