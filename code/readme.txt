zfs-lyr.c s - zfs layered driver.
app.c  - user space application - can work in driver mode corruption - transient
				  can work in raw mode corruption - absolute
app.h	- zfs specific data structures used by app.c
zfsapp.c - normal app to perform read/write
zilapp.c - app for generating zil blocks
compile - compiles the driver
initialize - sample init/finish scripts used by us for performing the testing. one will need to change it as per environment.
finish
