#!/usr/bin/env python

"""
setup.py file for SWIG
"""

from distutils.core import setup, Extension
from distutils.command import clean
import distutils.util as util
import distutils.sysconfig as sysconfig
import os


# Remove warning on function declaration with no parameters omitting void.
vars_dict = sysconfig.get_config_vars()
vars_dict['CFLAGS'] = sysconfig.get_config_var('CFLAGS').replace('-Wstrict-prototypes', '')
vars_dict['OPT'] =  sysconfig.get_config_var('OPT').replace('-Wstrict-prototypes', '')

platform = util.get_platform()

if platform.find('macosx') == 0:
    # This is necessary because extra_link_args are actually misplaced in order to clang/gcc to be able to link properly. So here is a workaround
    os.environ['LDFLAGS'] = '-framework CoreServices'

    NtpSync_module = Extension('_NtpSyncPy',
                                define_macros = [('MAJOR_VERSION', '1'),('MINOR_VERSION', '0')],
                                sources = ['NtpSyncPy_wrap.c', 'UdpConn.c', 'NtpSync.c', 'DebugUtil.c'],
                                include_dirs = [],
                                library_dirs = ['/System/Library/Frameworks/CoreServices.framework'],
                                libraries = ['pthread'],
                                extra_compile_args = [ '-Wno-deprecated-declarations', '-Wno-self-assign', '-Wno-error=no-self-assign'],
                                extra_link_args = ['-framework CoreServices'])

elif platform.find('linux') == 0:

    NtpSync_module = Extension('_NtpSyncPy',
                                define_macros = [('MAJOR_VERSION', '1'),('MINOR_VERSION', '0')],
                                sources = ['NtpSyncPy_wrap.c', 'UdpConn.c', 'NtpSync.c', 'DebugUtil.c'],
                                include_dirs = [],
                                library_dirs = [],
                                libraries = [ 'pthread', 'rt' ],
                                extra_compile_args = [],
                                extra_link_args = [])
else:

    import sys
    print 'Unsupported platform: %s' %platform
    sys.exit(0)


setup (name = 'NtpSyncPy',
       version = '1.0',
       author = "Luca Filippin",
       author_email = "luca.filippin@gmail.com",
       description = """Python wrapper to NtpSync""",
       maintainer = "Luca Filippin",
       ext_modules = [NtpSync_module],
       py_modules = ["NtpSync"],
       )
