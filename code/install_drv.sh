#!/bin/bash
/usr/ccs/bin/ld -r -o zfs_lyr zfs_lyr.o

cp zfs_lyr.conf /usr/kernel/drv

cp zfs_lyr /tmp

rm /usr/kernel/drv/amd64/zfs_lyr
ln -s /tmp/zfs_lyr /usr/kernel/drv/amd64/zfs_lyr

#cp zfs_lyr /usr/kernel/drv/amd64/zfs_lyr

add_drv zfs_lyr
