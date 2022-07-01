#!/bin/sh
TMP_DIR=$(mktemp -d)
DL_NAME="tmp.tar.bz2"
DL_NAME_DESKTOP="tmp.desktop"

if [ -d "/opt/thunderfox" ]; then
  echo "Thunderfox is already installed."
  exit 0
fi

wget -O "${TMP_DIR}/${DL_NAME}" "https://thunderfox.page.link/download_release_linux_x86_64"
wget -O "${TMP_DIR}/${DL_NAME_DESKTOP}" "https://github.com/typeling1578/Thunderfox/raw/HEAD/browser/linux-installer/thunderfox.desktop"

tar xf "${TMP_DIR}/${DL_NAME}" --one-top-level=thunderfox --directory="${TMP_DIR}"

sudo mv "${TMP_DIR}/thunderfox" /opt/thunderfox/
sudo ln -s /opt/thunderfox/thunderfox /usr/local/bin/thunderfox

xdg-desktop-menu install --novendor "${TMP_DIR}/tmp.desktop"

rm -r ${TMP_DIR}
