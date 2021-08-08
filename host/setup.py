from setuptools import setup

with open("../README.md", "r") as f:
    long_description = f.read()

setup(
    name='dmctl',
    version='0.1.0',
    description='Tool for controlling a dmj device',
    long_description=long_description,
    long_description_content_type='text/markdown',
    url='',
    author='sys64738',
    author_email='sys64738@disroot.org',
    license='GPL-3.0',
    packages=['dmctl'],
    install_requires=[
        "pyusb>=1.1.1"
    ],
    python_requires=">=3.9",
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: GNU General Public License v3",
        "Operating System :: Unix",
        "Topic :: Software Development :: User Interfaces",
        "Topic :: System :: Hardware :: Hardware Drivers",
        "Topic :: Terminals :: Serial",
        "Topic :: Utilities",
        "Typing :: Typed"
    ],
    include_package_data=True,
    entry_points={
        'console_scripts': [
            "dmctl=dmctl:main"
        ]
    },
    zip_safe=False
)

