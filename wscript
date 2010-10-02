#! /usr/bin/env python

VERSION = '0.1'
APPNAME = 'dlib'

srcdir = '.'
blddir = 'build'

import Options
import os, sys, waf_dynamo

def init():
    pass

def set_options(opt):
    opt.sub_options('src')
    opt.tool_options('compiler_cxx')
    opt.tool_options('waf_dynamo')

def configure(conf):
    conf.check_tool('compiler_cxx')
    conf.check_tool('waf_dynamo')
    conf.sub_config('src')

    conf.check_tool('java')

    platform = conf.env['PLATFORM']

    if platform == "win32":
        conf.env.append_value('CPPPATH', "../include/win32")

    dynamo_home = os.getenv('DYNAMO_HOME')
    if not dynamo_home:
        conf.fatal("DYNAMO_HOME not set")
    dynamo_ext = os.path.join(dynamo_home, "ext")

    conf.env.append_value('CPPPATH', os.path.join(dynamo_ext, "include"))
    conf.env.append_value('LIBPATH', os.path.join(dynamo_ext, "lib", platform))

    conf.env['LIB_GTEST'] = 'gtest'

    if platform == "linux":
        conf.env['LIB_THREAD'] = 'pthread'
        conf.env['LIB_PLATFORM_SOCKET'] = ''
    elif 'darwin' in platform:
        conf.env['LIB_THREAD'] = ''
        conf.env['LIB_PLATFORM_SOCKET'] = ''
    else:
        conf.env['LIB_THREAD'] = ''
        conf.env['LIB_PLATFORM_SOCKET'] = 'WS2_32'

    conf.env.append_unique('CCDEFINES', 'DLIB_LOG_DOMAIN="DLIB"')
    conf.env.append_unique('CXXDEFINES', 'DLIB_LOG_DOMAIN="DLIB"')

def build(bld):
    bld.add_subdirs('src')
    bld.install_files('${PREFIX}/include/win32', 'include/win32/*.h')

def shutdown():
    if not Options.commands['build'] or getattr(Options.options, 'skip_tests', False):
        return

    # TODO: Fix support for win32
    from Logs import warn, error
    import urllib2, time, atexit

    if sys.platform != 'win32':
        os.system('scripts/start_proxy_server.sh')
        os.system('scripts/start_http_server.sh')
        atexit.register(os.system, 'scripts/stop_http_server.sh')
        atexit.register(os.system, 'scripts/stop_proxy_server.sh')

        start = time.time()
        while True:
            if time.time() - start > 5:
                error('HTTP server failed to start within 5 seconds')
                sys.exit(1)
            try:
                urllib2.urlopen('http://localhost:7000')
                break
            except urllib2.URLError:
                print('Waiting for HTTP testserver to start...')
                sys.stdout.flush()
                time.sleep(0.5)
    else:
        warn('HTTP tests not supported on Win32 yet')

    waf_dynamo.run_gtests(valgrind = True)
