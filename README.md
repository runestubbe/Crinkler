# Crinkler

Crinkler is a compressing linker for Windows, specifically targeted towards executables with a size of just a few kilobytes. Its main purpose is as a tool for producing small [demoscene](https://en.wikipedia.org/wiki/Demoscene) productions.

Download the latest version from the [Releases page](https://github.com/runestubbe/Crinkler/releases).

Usage information and version history can be found in the [manual](doc/manual.txt).

For general discussion, questions and comments, use the [Pouët.net forum](http://www.pouet.net/prod.php?which=18158).

## Distribution

Crinkler is mainly being developed by Rune L. H. Stubbe (Mentor/TBC) and Aske Simon Christensen (Blueberry/Loonies). It is distributed under the [Zlib license](https://en.wikipedia.org/wiki/Zlib_License).

You are welcome to integrate Crinkler into your own tools or toolchains. If you do so, preferably base your work on the `master` branch. This way, the Crinkler version identifier written into output executables (two digits at offset 2 in the file) will match the actual contents produced by Crinkler. All feature development that significantly affects the output from Crinkler will generally take place on other branches.

## Building

Build Crinkler using Visual Studio 2017 or later. The custom build rules for the assembly files require that `nasm.exe` is in the executable path.

The data compressor itself is separated out into its own library, located in the `Compressor` project. This library enables tools to estimate how big a particular piece of data would be after being compressed by Crinkler. Take a look at the `CompressorExample` project for a description of its usage.
