#!/bin/bash
#Works for 1GB Pen Drive
echo "Beginning Dump..."
dd if=/dev/sdb1 of=bin/usb.img bs=512 count=1968128
echo "Finished Dump!"
