[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=BenWibking_TwoMomentRad&metric=alert_status&token=5049c56ffe08dcc83afd5ca4c8e0d951a2836652)](https://sonarcloud.io/dashboard?id=BenWibking_TwoMomentRad)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=BenWibking_TwoMomentRad&metric=bugs&token=5049c56ffe08dcc83afd5ca4c8e0d951a2836652)](https://sonarcloud.io/dashboard?id=BenWibking_TwoMomentRad)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=BenWibking_TwoMomentRad&metric=ncloc&token=5049c56ffe08dcc83afd5ca4c8e0d951a2836652)](https://sonarcloud.io/dashboard?id=BenWibking_TwoMomentRad)
[![Documentation Status](https://readthedocs.org/projects/quokka-code/badge/?version=latest)](https://quokka-code.readthedocs.io/en/latest/?badge=latest)
[![AMReX](https://amrex-codes.github.io/badges/powered%20by-AMReX-red.svg)](https://amrex-codes.github.io)
[![yt-project](https://img.shields.io/static/v1?label="works%20with"&message="yt"&color="blueviolet")](https://yt-project.org)

# Quokka
*Quadrilateral, Umbra-producing, Orthogonal, Kangaroo-conserving Kode for Astrophysics!*

**The Quokka methods paper is now available: https://arxiv.org/abs/2110.01792**

**NOTE: The code documentation is still a work in progress. Please see the Installation Notes below. You can start a [Discussion](https://github.com/BenWibking/quokka/discussions) for technical support, or open an [Issue](https://github.com/BenWibking/quokka/issues) for any bug reports.**

Quokka is a two-moment radiation hydrodynamics code that uses the piecewise-parabolic method, with AMR and subcycling in time. Runs on CPUs (MPI+vectorized) or NVIDIA GPUs (MPI+CUDA) with a single-source codebase. Written in C++17. (100% Fortran-free.)

...with advanced Adaptive Quokka Refinement:tm: technology:

![Image of Quokka with Baby in Pouch](extern/quokka2.png)

## Installation Notes

To run Quokka, download this repository to your local machine:
```
git clone git@github.com:BenWibking/quokka.git
```
Then download all submodules (this downloads `AMReX` and the string-formatting library `fmt`):
```
cd quokka
git submodule update --init
```
Create a build/ subdirectory and compile Quokka:
```
mkdir build; cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j6
```
Congratuations! You have now built all of the 1D test problems on CPU. You can run the automated test suite:
```
make test
```
You should see output that indicates all tests have passed, like this:
```
100% tests passed, 0 tests failed out of 20

Total Test time (real) = 111.74 sec
```
To run in 2D or 3D, edit the `AMReX_SPACEDIM` option in the `CMakeLists.txt` file, for example:
```
set(AMReX_SPACEDIM 3 CACHE STRING "" FORCE)
```
to compile Quokka for 3D problems.

Have fun!

## Running on GPUs
By default, Quokka compiles itself to run only on CPUs. If you want to run on NVIDIA GPUs, re-build Quokka with the following options:
```
cmake .. -DCMAKE_BUILD_TYPE=Release -DAMReX_GPU_BACKEND=CUDA
make -j6
```
The compiled test problems are in the test problem subdirectories in `build/src/`. Example scripts for running Quokka on compute clusters are in the `scripts/` subdirectory. Please note that you must configure your compute cluster to run with 1 MPI rank per GPU in order for Quokka to work correctly. Quokka is only supported on Volta-class (V100) GPUs or newer.

**AMD or Intel GPUs:** Running on AMD or Intel GPUs is currently experimental and has *not been tested* by the Quokka developers. AMReX is currently undergoing rapid advances in its support for GPUs from these vendors, so please get in touch by starting a Discussion before attempting this.

## Problems?
If you run into problems, please start a [Discussion](https://github.com/BenWibking/quokka/discussions) for technical support. If you discover a bug, please let us know by opening an [Issue](https://github.com/BenWibking/quokka/issues).