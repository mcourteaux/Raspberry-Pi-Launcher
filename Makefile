EXTRA_SEARCH_PATHS = -I/opt/local/include -L/opt/local/lib

launcher: launcher.cpp
	g++ -std=c++20 -g -O2 launcher.cpp -o launcher -lSDL3 -lSDL3_image -lSDL3_ttf ${EXTRA_SEARCH_PATHS}

sudoers:
	echo "$(whoami) ALL=NOPASSWD: /sbin/reboot, /sbin/shutdown" > /etc/sudoers.d/010_rpi-launcher

systemd-service:
	cp systemd.service /lib/systemd/system/rpi-launcher.service
	systemctl daemon-reload
	systemctl enable rpi-launcher.service

