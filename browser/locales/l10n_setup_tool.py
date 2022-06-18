#!/usr/bin/env python3

import json
import shutil
import os
import sys
import urllib.request

directory = os.path.dirname(__file__).replace("\\","/")

l10n_list_file = open(directory + "/l10n-changesets.json", "r")
l10n_list = json.loads(l10n_list_file.read())
l10n_list_file.close()
l10n_available = l10n_list.keys()

def l10n_download():
    for lang in l10n_available:
        revision = l10n_list[lang]["revision"]
        url = "https://hg.mozilla.org/l10n-central/" + lang + "/archive/" + revision + ".zip"
        print("Downloading: " + lang)
        data = urllib.request.urlopen(url)
        if(data.getcode() == 200):
            file = open(directory + "/l10n/" + lang + ".zip", "wb")
            file.write(data.read())
            file.close()
            data.close()
        else:
            print("Error: Status Code Failure (" + str(data.status_code) + ")")
            break

def l10n_unpack():
    for lang in l10n_available:
        revision = l10n_list[lang]["revision"]
        path = directory + "/l10n/" + lang + ".zip"
        if(not os.path.isfile(path)):
            print("Warning: " + path + " is not found")
            continue
        print("Unpacking: " + path)
        from_name = directory + "/l10n/" + lang + "-" + revision
        to_name = directory + "/l10n/" + lang
        if(os.path.isdir(from_name)):
            shutil.rmtree(from_name)
        if(os.path.isdir(to_name)):
            shutil.rmtree(to_name)
        shutil.unpack_archive(path, directory + "/l10n")
        os.rename(from_name, to_name)

def l10n_pack_remove():
    for lang in l10n_available:
        path = directory + "/l10n/" + lang + ".zip"
        if(not os.path.isfile(path)):
            print("Warning: " + path + " is not found")
            continue
        print("Removing: " + path)
        os.remove(path)

def l10n_unpack_remove():
    for lang in l10n_available:
        path = directory + "/l10n/" + lang
        if(not os.path.isdir(path)):
            print("Warning: " + path + " is not found")
            continue
        print("Removing: " + path)
        shutil.rmtree(path)

def l10n_patch():
    for lang in l10n_available:
        lang_dir = directory + "/l10n/" + lang
        if(not os.path.isdir(lang_dir)):
            print("Warning: " + lang_dir + " is not found")
            continue

        print("Patching: " + lang_dir)

        official_folder = directory + "/l10n/" + lang + "/browser/branding/official"
        if(os.path.isdir(official_folder)):
            shutil.rmtree(official_folder)
        
        channel = "release"
        from_folder = directory + "/l10n_patch/branding/" + channel
        to_folder = directory + "/l10n/" + lang + "/browser/branding/" + channel
        if(not os.path.isdir(to_folder)):
            os.mkdir(to_folder)
        shutil.copyfile(from_folder + "/brand.dtd", to_folder + "/brand.dtd")
        shutil.copyfile(from_folder + "/brand.ftl", to_folder + "/brand.ftl")
        shutil.copyfile(from_folder + "/brand.properties", to_folder + "/brand.properties")

        channel = "beta"
        from_folder = directory + "/l10n_patch/branding/" + channel
        to_folder = directory + "/l10n/" + lang + "/browser/branding/" + channel
        if(not os.path.isdir(to_folder)):
            os.mkdir(to_folder)
        shutil.copyfile(from_folder + "/brand.dtd", to_folder + "/brand.dtd")
        shutil.copyfile(from_folder + "/brand.ftl", to_folder + "/brand.ftl")
        shutil.copyfile(from_folder + "/brand.properties", to_folder + "/brand.properties")

try:
    do = sys.argv[1]
except Exception:
    do = ""
try:
    options = sys.argv[2:]
except Exception:
    options = []
if(do == "full-setup"):
    l10n_download()
    l10n_unpack()
    l10n_pack_remove()
    l10n_patch()
elif(do == "l10n-download"):
    l10n_download()
elif(do == "l10n-unpack"):
    l10n_unpack()
elif(do == "l10n-pack-remove"):
    l10n_pack_remove()
elif(do == "l10n-unpack-remove"):
    l10n_unpack_remove()
elif(do == "l10n-patch"):
    l10n_patch()
else:
    help = [
        "l10n_setup_tool.py COMMAND [OPTIONS]",
        "",
        "Commands:",
        "\tfull-setup                Full setup",
        "\tl10n-download             Download l10n packages",
        "\tl10n-unpack               Unpack l10n packages",
        "\tl10n-pack-remove          Remove l10n packages",
        "\tl10n-unpack-remove        Remove l10n unpackages",
        "\tl10n-patch                Apply patch to l10n"
    ]
    print("\n".join(help))
