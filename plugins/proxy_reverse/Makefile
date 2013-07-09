all:
	gcc -g -I/root/monkey/src/include/ -fPIC -shared -Wl,-soname,plugin plugin.c string.c dictionary.c -o plugin.so

test:
	gcc -g -I/root/monkey/src/include/ -fPIC -shared -Wl,-soname,plugintest plugintest.c string.c dictionary.c config.c -o plugintest.so

