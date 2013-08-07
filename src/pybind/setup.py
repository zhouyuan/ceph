from setuptools import setup
import os


def long_description():
    readme = os.path.join(os.path.dirname(__file__), 'README.rst')
    return open(readme).read()


setup(
    name             = 'ceph',
    description      = 'Bindings for Ceph',
    packages         = ['ceph'],
    author           = 'Inktank',
    author_email     = 'ceph-devel@vger.kernel.org',
    version          = '0.0.1',  #XXX Fix version
    license          = "GPLv2",
    zip_safe         = False,
    keywords         = "ceph, bindings, api, cli",
    long_description = "",  #XXX Long description should come from the README.rst
    classifiers      = [
        'Development Status :: 5 - Production/Stable',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: GNU General Public License v2 (GPLv2)',
        'Topic :: Software Development :: Libraries',
        'Topic :: Utilities',
        'Topic :: System :: Filesystems',
        'Operating System :: POSIX',
        'Programming Language :: Python :: 2.6',
        'Programming Language :: Python :: 2.7',
    ],
)
