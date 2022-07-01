#!/bin/sh
TMP_DIR=$(mktemp -d)
DL_NAME="tmp.tar.bz2"
DL_NAME_DESKTOP="tmp.desktop"

if [ -d "/opt/thunderfox-beta" ]; then
  echo "Thunderfox Beta is already installed."
  exit 0
fi

echo "Downloading..."
wget -O "${TMP_DIR}/${DL_NAME}" "https://thunderfox.page.link/download_beta_linux_x86_64"
wget -O "${TMP_DIR}/${DL_NAME_DESKTOP}" "https://github.com/typeling1578/Thunderfox/raw/HEAD/browser/linux-installer/thunderfox-beta.desktop"

echo "Extracting..."
tar xf "${TMP_DIR}/${DL_NAME}" --one-top-level=thunderfox --directory="${TMP_DIR}"

echo "Installing..."
sudo mv "${TMP_DIR}/thunderfox" /opt/thunderfox-beta/
#sudo ln -s /opt/thunderfox-beta/thunderfox /usr/local/bin/thunderfox

xdg-desktop-menu install --novendor "${TMP_DIR}/tmp.desktop"

echo "Installation complete!"

rm -r ${TMP_DIR}
