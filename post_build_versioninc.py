Import("env","projenv")

def get_cppdefine_value(flag_name):
    build_flags = env.ParseFlags(env['BUILD_FLAGS'])
    flags_with_value_list = [build_flag for build_flag in build_flags.get('CPPDEFINES') if type(build_flag) == list]
    defines = {k: v for (k, v) in flags_with_value_list}
    return defines.get(flag_name)

FILENAME_BUILDNO = 'versioning'

build_no = 0
try:
    with open(FILENAME_BUILDNO) as f:
        build_no = int(f.readline()) + 1
except:
    print('Starting build number from 1..')
    build_no = 1
with open(FILENAME_BUILDNO, 'w+') as f:
    f.write(str(build_no))
    print('Build number: {}'.format(build_no))
projenv.Append(CPPDEFINES=[("BUILD_NUMBER",str(build_no))])

z = "\\\""+get_cppdefine_value("BUILD_MAJOR_RELEASE")+"."+get_cppdefine_value("BUILD_MINOR_RELEASE")+"."+str(build_no)+"\\\""
print('Build version: {}'.format(str(z)))
projenv.Append(CPPDEFINES=[("BUILD_VERSION_STRING",str(z))])


#projenv.Append(CPPDEFINES=[("BUILD_VERSION",get_cppdefine_value("BUILD_MAJOR_RELEASE")+"."+get_cppdefine_value("BUILD_MINOR_RELEASE")+"."+str(build_no))])
