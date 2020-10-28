from distutils.core import setup
from distutils.extension import Extension
from Cython.Build import cythonize
import os

# e.g. ../build/release
build_dir = os.environ['CBUILD_DIR']
extra_compile_args = None
extra_link_args = None
# This shit don't work
if 'debug' in build_dir:
    extra_compile_args = ['-fsanitize=address']
    extra_link_args = ['-fsanitize=address']

pystructland_extension = Extension(
    name="pystructlang",
    sources=["pystructlang.pyx"],
    libraries=["structlang"],
    library_dirs=[build_dir],
    include_dirs=["../src", f"{build_dir}/src"],
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
)
setup(
    name="pystructlang",
    ext_modules=cythonize([pystructland_extension])
)
