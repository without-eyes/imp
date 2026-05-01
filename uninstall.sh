#!/bin/bash

set -e

RED='\033[1;31m'
GREEN='\033[1;32m'
BLUE='\033[1;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}     I.M.P. System Uninstaller          ${NC}"
echo -e "${BLUE}========================================${NC}"

echo -e "\n${YELLOW}Stopping I.M.P. daemon if running...${NC}"
sudo pkill -f /usr/bin/imp || true

echo -e "\n${RED}[1/3] Removing core binary and modules...${NC}"
sudo rm -f /usr/bin/imp
sudo rm -rf /usr/lib/imp
echo "  -> /usr/bin/imp removed."
echo "  -> /usr/lib/imp/ removed."

echo -e "\n${YELLOW}[2/3] Do you want to remove configuration files? (/etc/imp) [y/N]${NC}"
read -r response
if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
    sudo rm -rf /etc/imp
    echo -e "${GREEN}Configuration removed.${NC}"
else
    echo -e "${BLUE}Configuration preserved.${NC}"
fi

echo -e "\n${YELLOW}[3/3] Do you want to remove all logs? (/var/log/imp) [y/N]${NC}"
read -r response
if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
    sudo rm -rf /var/log/imp
    echo -e "${GREEN}Logs removed.${NC}"
else
    echo -e "${BLUE}Logs preserved.${NC}"
fi

sudo rm -f /var/run/imp.pid
sudo rm -f /tmp/imp.pid
sudo rm -f /tmp/imp_dashboard.sock

echo -e "\n${BLUE}========================================${NC}"
echo -e "${GREEN}I.M.P. has been successfully uninstalled!${NC}"
echo -e "${BLUE}========================================${NC}"