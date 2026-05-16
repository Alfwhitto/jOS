VMware Workstation Pro Notes

This OS now includes a more BIOS-friendly stage-1 loader for VMware-style
firmware behavior:

- The bootloader enables A20 explicitly.
- The bootloader reads the kernel in exact-size BIOS EDD chunks.
- Each chunk is limited to 127 sectors and uses a segment-aligned buffer.

Recommended VM configuration

- Firmware: Legacy BIOS
- Guest type: Other 32-bit
- Boot disk: `os.vmdk`
- Rootfs disk: `rootfs.vmdk`
- Disk controller for both disks: IDE

Expected disk layout inside the guest

- IDE primary master: boot disk containing the boot sector and kernel image
- IDE primary slave: FAT rootfs disk

Why IDE matters

The kernel's storage driver currently talks directly to the legacy primary ATA
ports (`0x1F0` / `0x3F6`) and probes only the primary master/slave pair. If
VMware attaches the rootfs as SATA, SCSI, or NVMe, BIOS boot may still work but
the kernel will not find the rootfs after handoff.

Current networking limitation

The kernel only contains a driver for `rtl8139`. VMware Workstation Pro does
not normally emulate that NIC, so networking is expected to stay offline in
VMware until a VMware-compatible NIC driver is added.
