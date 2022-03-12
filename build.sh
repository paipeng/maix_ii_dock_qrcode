#!/bin/bash

python3 project.py distclean
export PATH=/opt/cmake-3.23.0-rc3-linux-x86_64/bin:$PATH
python3 project.py --toolchain /opt/toolchain-sunxi-musl/toolchain/bin --toolchain-prefix arm-openwrt-linux-muslgnueabi- config
#python3 project.py menuconfig
python3 project.py build
#python3 project.py upload

scp dist/maix_ii_dock_qrcode root@192.168.1.163:maix_dist
scp dist/start_app.sh root@192.168.1.163:maix_dist

#python3 project.py clean
#python3 project.py distclean
