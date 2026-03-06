Import("env", "projenv")
import json
import os

# Fonction pour récupérer les valeurs des drapeaux de compilation
def get_cppdefine_value(flag_name):
    build_flags = env.ParseFlags(env['BUILD_FLAGS'])
    flags_with_value_list = [build_flag for build_flag in build_flags.get('CPPDEFINES') if type(build_flag) == list]
    defines = {k: v for (k, v) in flags_with_value_list}
    return defines.get(flag_name, "0")

FILENAME_BUILDNO = 'versioning'
FILENAME_JSON = 'library.json'

# 1. Gestion du numéro de build (Incrémentation)
build_no = 0
try:
    with open(FILENAME_BUILDNO) as f:
        build_no = int(f.readline()) + 1
except:
    print('Starting build number from 1..')
    build_no = 1

# Sauvegarde du nouveau numéro de build
with open(FILENAME_BUILDNO, 'w+') as f:
    f.write(str(build_no))
    print('Build number: {}'.format(build_no))

# 2. Récupération de la version majeure/mineure depuis platformio.ini
# Attention : get_cppdefine_value peut retourner des chaînes avec des guillemets (ex: "0")
major = get_cppdefine_value("BUILD_MAJOR_RELEASE").replace('"', '')
minor = get_cppdefine_value("BUILD_MINOR_RELEASE").replace('"', '')

# Construction de la version "propre" pour library.json (ex: 0.2.1978)
raw_version_string = "{}.{}.{}".format(major, minor, build_no)

# Construction de la version échappée pour le C++ (ex: "\"0.2.1978\"")
cpp_version_string = "\\\"" + raw_version_string + "\\\""

print('Build version: {}'.format(raw_version_string))

# 3. Injection dans le code C++ (Macros)
projenv.Append(CPPDEFINES=[("BUILD_NUMBER", str(build_no))])
projenv.Append(CPPDEFINES=[("BUILD_VERSION_STRING", cpp_version_string)])

# 4. Mise à jour automatique de library.json
try:
    # On utilise PROJECT_DIR pour être sûr de trouver le fichier
    json_path = os.path.join(env['PROJECT_DIR'], FILENAME_JSON)
    
    if os.path.exists(json_path):
        with open(json_path, 'r') as f:
            data = json.load(f)
        
        # Mise à jour du champ version
        old_version = data.get('version', 'unknown')
        if old_version != raw_version_string:
            data['version'] = raw_version_string
            
            with open(json_path, 'w') as f:
                json.dump(data, f, indent=2)
            print("Updated library.json version from {} to {}".format(old_version, raw_version_string))
        else:
            print("library.json already up to date.")
    else:
        print("Warning: {} not found at {}".format(FILENAME_JSON, json_path))

except Exception as e:
    print("Error updating library.json: {}".format(str(e)))


print("-" * 40)
print(f"   How to publish version {raw_version_string}")
print("-" * 40)
print(f"1. git add library.json versioning")
print(f"2. git commit -m \"Release version {raw_version_string}\"")
print(f"3. git tag v{raw_version_string}")
print(f"4. git push origin master --tags")
print(f"5. pio account login")
print(f"6. pio package publish")
print("-" * 40)