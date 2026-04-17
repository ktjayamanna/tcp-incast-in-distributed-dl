from setuptools import find_namespace_packages, setup


setup(
    name="traffic-sim",
    version="0.1.0",
    description="Traffic generation and validation utilities for incast simulation.",
    packages=find_namespace_packages(
        include=["traffic*"],
        exclude=["traffic.tests", "traffic.tests.*", "traffic.data", "traffic.data.*"],
    ),
    include_package_data=True,
    install_requires=[
        "numpy",
        "matplotlib",
        "openpyxl",
    ],
    python_requires=">=3.8",
)
