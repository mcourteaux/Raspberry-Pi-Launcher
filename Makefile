launcher: launcher.cpp
	g++ -g -O2 launcher.cpp -o launcher -lSDL3 -lSDL3_image -lSDL3_ttf

sudoers:
	echo "$(whoami) ALL=NOPASSWD: /sbin/reboot, /sbin/shutdown" > /etc/sudoers.d/010_rpi-launcher

