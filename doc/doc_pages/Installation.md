@page Installation Installing Phoebe

@section Download Download
The code is available (free of charge with an open-source MIT license) at its [github page](https://github.com/mir-group/phoebe).
If checking out from the GitHub repository, make sure to use the master branch for production. The developer's branch, while we will attempt to keep it in working condition, is not recommended for public usage.


@section Prerequisites Prerequisites
The installation requires the following packages pre-installed:
* CMake (a recent version);
* a C++ compiler with C++14 support. We regularly test with GCC, but Intel and Clang should work too;
* MPI (although the code can compile without);
* Optional: OpenMP;
* Optional: CUDA (for GPU acceleration);
* Internet connection, to download external libraries.



@section Cmake Cmake build

@subsection basic Basic build

To install Phoebe, type:
~~~~~~~~~~~~~~~{.c}
git submodule update --init
mkdir build
cd build
cmake ..
make -j$(nproc)
~~~~~~~~~~~~~~~
where you should substitute `nproc` with a number of parallel compilation jobs.
This will create the executable `phoebe` in the `build` directory.

CMake will inspect the paths found in the environmental variable `LD_LIBRARY_PATH` to verify the existence of an installed copy of the SCALAPACK library and link it. If not found, the installation will compile a copy of the SCALAPACK.


@subsection OpenMP OpenMP build
~~~~~~~~~~~~~~~{.c}
git submodule update --init
mkdir build
cd build
cmake .. -DKokkos_ENABLE_OPENMP=ON -DOMP_AVAIL=ON
make -j$(nproc)
~~~~~~~~~~~~~~~

@subsection CUDA OpenMP + CUDA build
~~~~~~~~~~~~~~~{.c}
git submodule update --init
mkdir build
cd build
cmake .. -DKokkos_ENABLE_CUDA=ON -DKokkos_ARCH_VOLTA70=ON -DOMP_AVAIL=ON
make -j$(nproc)
~~~~~~~~~~~~~~~
Replace VOLTA70 (e.g. for V100s GPUs) with the arch of your GPU.

@subsection Kokkos Note on Kokkos
Phoebe utilizes Kokkos to generate either OpenMP or CUDA code to accelerate parts of the code.
Thus Phoebe accepts all the CMake arguments of Kokkos, which can improve performance.
For example, you should specify -DKokkos_ARCH_KNL=ON when building for Knight's Landing nodes.








@section Doc Compiling the documentation
In order to compile the documentation, you need to have installed on your machine:
* doxygen
* graphviz 
* pdflatex (to render equations)
Typically for Unix machines, these packages are commonly found on package managers (apt, pacman, brew, ...).

Then type:
~~~~~~~~~~~~~~~~~~~{.c}
cd build
make doc
~~~~~~~~~~~~~~~~~~~

Note that compiling the documentation doesn't require compiling the code.



@section Install Installation instructions for common workstations and supercomputers

@subsection Cannon Cannon @ Holyoke

@subsection NERSC Cori @ NERSC

@subsection TACC Ranch @ TACC

@subsection Ubuntu Ubuntu
To install (without GPU support):
~~~~~~~~~~~~~~~~~~~{.c}
sudo apt install cmake gcc doxygen graphviz libomp-dev libopenmpi3
git submodule update --init
mkdir build
cd build
cmake .. -DKokkos_ENABLE_OPENMP=ON -DOMP_AVAIL=ON
make -j$(nproc)
make doc
~~~~~~~~~~~~~~~~~~~
Tested on Ubuntu 20.04.

@subsection Mac MacOs 
We have experienced troubles linking the SCALAPACK library, especially when linking it together with the libgfortran library.
If libgfortran is not found, try adding it specifically to LD_LIBRARY_PATH:
~~~~~~~~~~~~~~~~~~~{.c}
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/path/to/libgfortran/
~~~~~~~~~~~~~~~~~~~
In particular, if you are using a version of gcc installed using homebrew, you might need to link the `Cellar` copy of libgfortran. As an example working for gcc v9.3.0_1 is: 
~~~~~~~~~~~~~~~~~~~{.c}
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/Cellar/gcc/9.3.0_1/lib/gcc/9/) 
~~~~~~~~~~~~~~~~~~~