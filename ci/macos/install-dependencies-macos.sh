#!/bin/sh



OSTYPE=$(uname)
MACOS_DEPS_VERSION=2020-08-30
QT_VERSION=5.14.1

if [ "${OSTYPE}" != "Darwin" ]; then
    echo "[Error] macOS install dependencies script can be run on Darwin-type OS only."
    exit 1
fi

HAS_BREW=$(type brew 2>/dev/null)

if [ "${HAS_BREW}" = "" ]; then
    echo "[Error] Please install Homebrew (https://www.brew.sh/) to build this plugin on macOS."
    exit 1
fi

# OBS Studio deps
echo "=> Updating Homebrew.."
brew update >/dev/null

echo "[=> Checking installed Homebrew formulas.."
BREW_PACKAGES=$(brew list --formula)
BREW_DEPENDENCIES="jack speexdsp ccache swig mbedtls"

for DEPENDENCY in ${BREW_DEPENDENCIES}; do
    if echo "${BREW_PACKAGES}" | grep -q "^${DEPENDENCY}\$"; then
        echo "=> Upgrading OBS-Studio dependency '${DEPENDENCY}'.."
        brew upgrade ${DEPENDENCY} 2>/dev/null
    else
        echo "=> Installing OBS-Studio dependency '${DEPENDENCY}'.."
        brew install ${DEPENDENCY} 2>/dev/null
    fi
done

# qtwebsockets deps
echo "=> Installing plugin dependency 'QT ${QT_VERSION}'.."

curl -L -O https://github.com/obsproject/obs-deps/releases/download/${MACOS_DEPS_VERSION}/macos-qt-${QT_VERSION}-${MACOS_DEPS_VERSION}.tar.gz
tar -xf ./macos-qt-${QT_VERSION}-${MACOS_DEPS_VERSION}.tar.gz -C "/tmp"
xattr -r -d com.apple.quarantine /tmp/obsdeps

# Fetch and install Packages app
# =!= NOTICE =!=
# Installs a LaunchDaemon under /Library/LaunchDaemons/fr.whitebox.packages.build.dispatcher.plist
# =!= NOTICE =!=

HAS_PACKAGES=$(type packagesbuild 2>/dev/null)

if [ "${HAS_PACKAGES}" = "" ]; then
    echo "=> Installing Packaging app (might require password due to 'sudo').."
    curl -o './Packages.pkg' --retry-connrefused -s --retry-delay 1 'https://s3-us-west-2.amazonaws.com/obs-nightly/Packages.pkg'
    sudo installer -pkg ./Packages.pkg -target /
fi
