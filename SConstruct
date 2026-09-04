import os
import sys

# Suppress MSVC detection warning on Windows (SDK uses GCC cross-compiler)
try:
    import SCons.Warnings
    SCons.Warnings.suppressWarningClass(SCons.Warnings.VisualCMissingWarning)
except (ImportError, AttributeError):
    pass

# Luban-Lite root directory
AIC_ROOT = os.path.normpath(os.getcwd())

# luban-lite custom scripts
aic_script_path = os.path.join(AIC_ROOT, 'tools/scripts/')
sys.path.append(aic_script_path)
from aic_build import *
os.environ["AIC_ROOT"] = AIC_ROOT
os.environ["AIC_SCRIPT_DIR"] = aic_script_path
chk_prj_config(AIC_ROOT)
PRJ_CHIP,PRJ_BOARD,PRJ_KERNEL,PRJ_APP,PRJ_DEFCONFIG_NAME,PRJ_CUSTOM_LDS,PRJ_TOOLCHAIN_VER = get_prj_config(AIC_ROOT)
PRJ_NAME = PRJ_DEFCONFIG_NAME.replace('_defconfig','')
PRJ_OUT_DIR = 'output/' + PRJ_NAME + '/images/'
AIC_SCRIPT_DIR = aic_script_path
AIC_COMMON_DIR = os.path.join(AIC_ROOT, 'bsp/artinchip/sys/' + PRJ_CHIP)
AIC_PACK_DIR = os.path.join(AIC_ROOT, 'target/' + PRJ_CHIP + '/' + PRJ_BOARD + '/pack/')

# Var tranfer to SConscript
Export('AIC_ROOT')
Export('AIC_SCRIPT_DIR')
Export('AIC_COMMON_DIR')
Export('AIC_PACK_DIR')
Export('PRJ_CHIP')
Export('PRJ_BOARD')
Export('PRJ_KERNEL')
Export('PRJ_APP')
Export('PRJ_NAME')
Export('PRJ_DEFCONFIG_NAME')
Export('PRJ_OUT_DIR')
Export('PRJ_TOOLCHAIN_VER')
# Var tranfer to Kconfig 'option env=xxx'
os.environ["AIC_COMMON_DIR"]     = AIC_COMMON_DIR
os.environ["AIC_PACK_DIR"]       = AIC_PACK_DIR
os.environ["PRJ_CHIP"]           = PRJ_CHIP
os.environ["PRJ_BOARD"]          = PRJ_BOARD
os.environ["PRJ_KERNEL"]         = PRJ_KERNEL
os.environ["PRJ_APP"]            = PRJ_APP
os.environ["PRJ_NAME"]           = PRJ_NAME
os.environ["PRJ_DEFCONFIG_NAME"] = PRJ_DEFCONFIG_NAME
os.environ["PRJ_OUT_DIR"]        = PRJ_OUT_DIR
os.environ["PRJ_TOOLCHAIN_VER"]  = PRJ_TOOLCHAIN_VER

# rtconfig
chip_path = os.path.join(AIC_ROOT, 'bsp/artinchip/sys/' + PRJ_CHIP)
sys.path.append(chip_path)
import rtconfig

# RTT_ROOT
if os.getenv('RTT_ROOT'):
    RTT_ROOT = os.getenv('RTT_ROOT')
else:
    RTT_ROOT = os.path.join(AIC_ROOT, 'kernel/rt-thread/')
os.environ["RTT_ROOT"]           = RTT_ROOT
sys.path.append(os.path.join(RTT_ROOT, 'tools'))
from building import *

# ENV_ROOT
if os.getenv('ENV_ROOT') is None:
    ENV_ROOT = RTT_ROOT + '/../../tools/env'
    os.environ["ENV_ROOT"] =  ENV_ROOT

# TARGET
TARGET = PRJ_OUT_DIR + rtconfig.SOC + '.' + rtconfig.TARGET_EXT

# check QEMU
qemu_en = get_config('.config', 'CONFIG_QEMU_RUN')
if qemu_en:
    ld = os.path.join(chip_path, 'link_script', rtconfig.QEMU_LD_SCRIPT)
else:
    if not PRJ_CUSTOM_LDS:
        ld  = os.path.join(chip_path, 'link_script', rtconfig.LD_SCRIPT)
    else:
        ld  = os.path.join(AIC_ROOT, PRJ_CUSTOM_LDS)
    # Compile xxx.ld.S to xxxx.ld
    lds = ld + '.S'
    abs_gcc = os.path.join(rtconfig.EXEC_PATH, rtconfig.CC)
    cmd = abs_gcc + ' -E -P -<' + lds + ' > ' + ld
    os.system(cmd)
# Convert backslashes to forward slashes for GCC on Windows
# to avoid path separator issues in CI/Jenkins environments
if sys.platform == "win32":
    ld = ld.replace('\\', '/')
rtconfig.LFLAGS += ' -T ' + ld

# create env
env  = Environment(tools = ['mingw'],
MSVC_VERSION = '',
AS   = rtconfig.AS,   ASFLAGS   = rtconfig.AFLAGS,
CC   = rtconfig.CC,   CFLAGS   = rtconfig.CFLAGS,
CXX  = rtconfig.CXX,  CXXFLAGS  = rtconfig.CXXFLAGS,
AR   = rtconfig.AR,   ARFLAGS   = '-rc',
LINK = rtconfig.LINK, LINKFLAGS = rtconfig.LFLAGS,
RANLIBCOM = '')
env.PrependENVPath('PATH', rtconfig.EXEC_PATH)

# add --start-group and --end-group for GNU GCC
# Use TEMPFILE on all platforms to avoid "Argument list too long"
# when the link command exceeds Linux MAX_ARG_STRLEN (128KB).
if sys.platform == "win32":
    # On Windows, use Action to generate link command with forward slashes
    from SCons.Script import Action

    def win32_link_action(target, source, env):
        # Convert all paths to forward slashes for GCC
        sources = ' '.join([str(s).replace('\\', '/') for s in source])
        target_path = str(target[0]).replace('\\', '/')

        # Get library flags with forward slashes
        libdirflags = env.subst('$_LIBDIRFLAGS').replace('\\', '/')
        libflags = env.subst('$_LIBFLAGS').replace('\\', '/')
        linkflags = env.subst('$LINKFLAGS')
        rpath = env.subst('$__RPATH')
        link = env.subst('$LINK')

        # Build complete command
        cmd_args = [
            link, '-o', target_path,
            linkflags, rpath
        ]
        # Add source files
        cmd_args.extend([str(s).replace('\\', '/') for s in source])
        # Add library flags
        cmd_args.extend([libdirflags, '-Wl,--whole-archive', '-Wl,--start-group',
                        libflags, '-Wl,--end-group', '-Wl,--no-whole-archive'])

        # Filter out empty strings
        cmd_args = [arg for arg in cmd_args if arg]

        # Use TEMPFILE for long commands (check if length > 2048)
        cmd_line = ' '.join(cmd_args)
        if len(cmd_line) > 2048:
            import tempfile
            import os

            # Create response file
            fd, temp_path = tempfile.mkstemp(suffix='.rsp', text=True)
            try:
                with os.fdopen(fd, 'w') as f:
                    f.write(' '.join(cmd_args[1:]))  # Skip the linker executable

                # Execute with response file
                result = os.system(cmd_args[0] + ' @' + temp_path)
            finally:
                if os.path.exists(temp_path):
                    os.remove(temp_path)
            return result
        else:
            # Execute directly
            import os
            return os.system(cmd_line)

    win32_link_action.strfunction = lambda target, source, env: 'LINK ' + str(target[0])
    env['LINKCOM'] = Action(win32_link_action)
else:
    env['LINKCOM'] = "${TEMPFILE('$LINK -o $TARGET $LINKFLAGS $__RPATH $SOURCES $_LIBDIRFLAGS -Wl,--whole-archive -Wl,--start-group $_LIBFLAGS -Wl,--end-group -Wl,--no-whole-archive','$LINKCOMSTR')}"
env['ASCOM'] = env['ASPPCOM']

# signature database
env.SConsignFile(PRJ_OUT_DIR + ".sconsign.dblite")

Export('RTT_ROOT')
Export('rtconfig')

# Windows-specific path handling for GCC toolchain
if sys.platform == "win32":
    import re
    from SCons.Subst import quote_spaces

    # Replace backslashes with forward slashes for GCC on Windows
    # This prevents path corruption in command lines
    WINPATHSEP_RE = re.compile(r"\\([^\"'\\]|$)")

    def win_path_normalize(arg):
        """Normalize Windows paths for GCC by converting backslashes to forward slashes"""
        if not isinstance(arg, str):
            return arg
        arg = quote_spaces(arg)
        return WINPATHSEP_RE.sub(r"/\1", arg)

    # Set custom escape function for command line arguments
    env["TEMPFILEARGESCFUNC"] = win_path_normalize

    # Monkey-patch Node's get_abspath to ensure forward slashes
    import SCons.Node
    import SCons.Node.FS

    _orig_get_abspath = SCons.Node.FS.Entry.get_abspath

    def _patched_get_abspath(self):
        path = _orig_get_abspath(self)
        if isinstance(path, str):
            path = path.replace('\\', '/')
        return path

    SCons.Node.FS.Entry.get_abspath = _patched_get_abspath

# Var tranfer to building.py
env['AIC_ROOT']                  = AIC_ROOT
env['AIC_SCRIPT_DIR']            = AIC_SCRIPT_DIR
env['AIC_COMMON_DIR']            = AIC_COMMON_DIR
env['AIC_PACK_DIR']              = AIC_PACK_DIR
env['PRJ_CHIP']                  = PRJ_CHIP
env['PRJ_BOARD']                 = PRJ_BOARD
env['PRJ_KERNEL']                = PRJ_KERNEL
env['PRJ_NAME']                  = PRJ_NAME
env['PRJ_APP']                   = PRJ_APP
env['PRJ_DEFCONFIG_NAME']        = PRJ_DEFCONFIG_NAME
env['PRJ_OUT_DIR']               = PRJ_OUT_DIR

# prepare building environment
objs = PrepareBuilding(env, RTT_ROOT, has_libcpu=False)

# Generate Filesystem install POST_ACTION
fsinstall = GetInstallList()
PRJ_POST_ACTION = get_prj_post_action(AIC_ROOT,
                                          PRJ_CHIP,
                                          PRJ_BOARD,
                                          PRJ_KERNEL,
                                          PRJ_APP,
                                          PRJ_DEFCONFIG_NAME,
                                          fsinstall)
# add post action
rtconfig.POST_ACTION += PRJ_POST_ACTION

env.Clean(PRJ_OUT_DIR, PRJ_OUT_DIR)

# make a building
DoBuilding(TARGET, objs)

# Convert the pinmux.c file into a preprocessed file for pin conflict analysis
cpp_defines = ' '
cpp_path = ' '

for i in env['CPPDEFINES']:
    cpp_defines = cpp_defines + env['CPPDEFPREFIX'] + str(i[:][0]) + ' '
for path in env['CPPPATH']:
    related_cpppath = os.path.normpath(path).replace(AIC_ROOT + os.path.sep, '')
    if not related_cpppath.startswith(os.path.join('packages', 'artinchip', 'lvgl-ui')):
        cpp_path += ' -I' + related_cpppath

output_path = os.path.join(env['PRJ_OUT_DIR'], '.pinmux.i ')
toolchain_path = os.path.join('toolchain', 'bin', rtconfig.CC)
pinmux_src_path = os.path.join('target', env['PRJ_CHIP'],
                               env['PRJ_BOARD'], 'pinmux.c')

if os.path.exists(pinmux_src_path):
    precess_pinmux = toolchain_path + ' -E -o '+ output_path + '-c ' + env['CFLAGS'] + cpp_defines + cpp_path + ' ' + pinmux_src_path
    os.system(precess_pinmux)
