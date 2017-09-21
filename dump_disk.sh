#!/bin/bash
#Works for 1GB Pen Drive
echo "Beginning Dump..."
dd if=/dev/disk/by-label/fuse of=bin/usb.img bs=512 count=1968128
echo "Finished Dump!"
