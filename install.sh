#!/bin/bash

set -e

GREEN='\033[1;32m'
BLUE='\033[1;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}      I.M.P. System Installer           ${NC}"
echo -e "${BLUE}========================================${NC}"

echo -e "\n${GREEN}[1/5] Installing necessary dependencies...${NC}"
echo -e "${YELLOW}Updating apt and installing build tools, testing frameworks, and system utilities...${NC}"
sudo apt-get update
sudo apt-get install -y build-essential libcap2-bin libgtest-dev lcov

echo -e "\n${GREEN}[2/5] Building project...${NC}"
make clean
make

echo -e "\n${GREEN}[3/5] Creating system directories...${NC}"
sudo mkdir -p /etc/imp
sudo mkdir -p /usr/lib/imp/modules
sudo mkdir -p /var/log/imp
sudo chown $USER:$USER /var/log/imp

echo -e "\n${GREEN}[4/5] Installing files...${NC}"
sudo cp build/imp /usr/bin/imp
sudo chmod 755 /usr/bin/imp

if [ ! -f /etc/imp/imp.json ]; then
    sudo cp config/default.conf /etc/imp/imp.json
    sudo chmod 644 /etc/imp/imp.json
    echo "  -> Default config installed."
else
    echo "  -> Config already exists. Skipping to preserve user settings."
fi
sudo cp build/modules/*.so /usr/lib/imp/modules/
sudo chmod 755 /usr/lib/imp/modules/*.so

echo -e "\n${GREEN}[5/5] Setting kernel capabilities...${NC}"
sudo setcap cap_sys_nice+ep /usr/bin/imp

echo -e "\n${GREEN}Installation successful!${NC}"

echo -e "\n${BLUE}========================================"
echo "You can now run I.M.P. from anywhere:"
echo "  Start Daemon:      imp -d"
echo "  Open Dashboard:    imp -i"
echo -e "========================================${NC}"