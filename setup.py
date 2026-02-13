from setuptools import setup, find_packages
import sys

extra_compile_args = []
if sys.platform != "win32":
    extra_compile_args = ["-fPIC", "-Wall"]

setup(
    name="pyshell",
    version="1.0",
    packages=find_packages(),
    package_data={
        'pyshell': ['*.dll', '*.so', '*.dylib']
    },
    include_package_data=True,
)
