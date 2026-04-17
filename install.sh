#!/bin/bash

set -e

echo "========================================"
echo "      I.M.P. System Installer           "
echo "========================================"

echo "[1/4] Building project..."
make clean
make

echo "[2/4] Creating system directories..."
sudo mkdir -p /etc/imp
sudo mkdir -p /usr/lib/imp/modules
sudo mkdir -p /var/log/imp
sudo chown $USER:$USER /var/log/imp

echo "[3/4] Installing files..."
sudo cp build/imp /usr/bin/imp
sudo chmod 755 /usr/bin/imp

if [ ! -f /etc/imp/imp.json ]; then
    sudo cp config/default.conf /etc/imp/imp.json
    sudo chmod 644 /etc/imp/imp.json
    echo "  -> Default config copied to /etc/imp/imp.json"
else
    echo "  -> Config already exists in /etc/imp/imp.json, skipping overwrite."
fi

sudo cp build/modules/*.so /usr/lib/imp/modules/
sudo chmod 755 /usr/lib/imp/modules/*.so

echo "[4/4] Done!"
echo "========================================"
echo "Installation successful!"
echo ""
echo "You can now run I.M.P. from anywhere:"
echo "  Start Daemon:      imp -d"
echo "  Open Dashboard:    imp -i"
echo "========================================"