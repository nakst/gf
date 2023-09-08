#!/bin/sh

mkdir -p gf2.app/Contents/MacOS
echo "#include \"gf2.cpp\"" > gf2.mm
echo "BDNL????" > gf2.app/Contents/PkgInfo
echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" > gf2.app/Contents/Info.plist
echo "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">" >> gf2.app/Contents/Info.plist
echo "<plist version=\"1.0\"> <dict> <key>CFBundleExecutable</key> <string>gf2</string> <key>CFBundleIdentifier</key> <string>nakst.gf2</string> <key>CFBundlePackageType</key> <string>APPL</string> <key>LSMinimumSystemVersion</key> <string>10.14</string> <key>CFBundleName</key> <string>gf2</string> <key>CFBundleVersion</key> <string>1</string> <key>CFBundleShortVersionString</key> <string>1</string> <key>NSPrincipalClass</key> <string>NSApplication</string> <key>NSSupportsAutomaticGraphicsSwitching</key> <true/> <key>NSRequiresAquaSystemAppearance</key> <false/> </dict> </plist>" >> gf2.app/Contents/Info.plist
clang++ gf2.mm -std=c++17 -o gf2.app/Contents/MacOS/gf2 -g -O2 -framework Cocoa -pthread -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-missing-field-initializers || exit 1
rm gf2.mm

echo "==========================================================================================================="
echo "The macOS port is a work in progress! It is not ready to be used. Please contribute to the code if you can."
echo "==========================================================================================================="
