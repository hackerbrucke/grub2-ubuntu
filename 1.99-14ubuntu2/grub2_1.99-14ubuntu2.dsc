-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA256

Format: 1.0
Source: grub2
Binary: grub2, grub-linuxbios, grub-efi, grub-common, grub2-common, grub-emu, grub-pc-bin, grub-pc, grub-rescue-pc, grub-coreboot-bin, grub-coreboot, grub-efi-ia32-bin, grub-efi-ia32, grub-efi-amd64-bin, grub-efi-amd64, grub-ieee1275-bin, grub-ieee1275, grub-firmware-qemu, grub-yeeloong-bin, grub-yeeloong, grub-mount-udeb
Architecture: any-i386 any-amd64 any-powerpc any-ppc64 any-sparc any-mipsel i386 kopensolaris-i386 amd64 powerpc ppc64 sparc mipsel kfreebsd-i386 kfreebsd-amd64
Version: 1.99-14ubuntu2
Maintainer: Ubuntu Developers <ubuntu-devel-discuss@lists.ubuntu.com>
Uploaders: Robert Millan <rmh@debian.org>, Felix Zielcke <fzielcke@z-51.de>, Jordi Mallach <jordi@debian.org>, Colin Watson <cjwatson@debian.org>
Dm-Upload-Allowed: yes
Homepage: http://www.gnu.org/software/grub/
Standards-Version: 3.8.4
Vcs-Bzr: http://bazaar.launchpad.net/~ubuntu-core-dev/ubuntu/precise/grub2/precise
Build-Depends: debhelper (>= 7.4.2~), quilt (>= 0.46-7), patchutils, autoconf, automake, autogen (>= 1:5.10), python, flex (>= 2.5.35), bison, po-debconf, help2man, texinfo, gcc-4.5-multilib [i386 kopensolaris-i386 any-amd64 any-ppc64 any-sparc], gcc-4.5, libncurses5-dev, xfonts-unifont, libfreetype6-dev, gettext, libusb-dev [!hurd-any], libdevmapper-dev (>= 2:1.02.34) [linux-any], libgeom-dev (>= 8.2+ds1-1) [kfreebsd-any] | libgeom-dev (<< 8.2) [kfreebsd-any], libsdl1.2-dev [!hurd-any], xorriso (>= 0.5.6.pl00), qemu-kvm [i386 kfreebsd-i386 kopensolaris-i386 any-amd64], parted [!hurd-any], libfuse-dev (>= 2.8.4-1.4) [linux-any kfreebsd-any]
Build-Conflicts: autoconf2.13, libnvpair-dev, libzfs-dev
Package-List: 
 grub-common deb admin optional
 grub-coreboot deb admin extra
 grub-coreboot-bin deb admin extra
 grub-efi deb admin extra
 grub-efi-amd64 deb admin extra
 grub-efi-amd64-bin deb admin extra
 grub-efi-ia32 deb admin extra
 grub-efi-ia32-bin deb admin extra
 grub-emu deb admin extra
 grub-firmware-qemu deb admin extra
 grub-ieee1275 deb admin extra
 grub-ieee1275-bin deb admin extra
 grub-linuxbios deb admin extra
 grub-mount-udeb udeb debian-installer extra
 grub-pc deb admin optional
 grub-pc-bin deb admin optional
 grub-rescue-pc deb admin extra
 grub-yeeloong deb admin extra
 grub-yeeloong-bin deb admin extra
 grub2 deb admin extra
 grub2-common deb admin optional
Checksums-Sha1: 
 6d0536da38224e7caf94cf2531a5f921ac057b9b 4652619 grub2_1.99.orig.tar.gz
 c757f620c4a4335cb10500a1c7741d059047a4ca 355956 grub2_1.99-14ubuntu2.diff.gz
Checksums-Sha256: 
 b91f420f2c51f6155e088e34ff99bea09cc1fb89585cf7c0179644e57abd28ff 4652619 grub2_1.99.orig.tar.gz
 69bf4fedc67bbdf007dce9b66f9e99dea6f408f51bf987855dfb8d887f150e1f 355956 grub2_1.99-14ubuntu2.diff.gz
Files: 
 ca9f2a2d571b57fc5c53212d1d22e2b5 4652619 grub2_1.99.orig.tar.gz
 eae1ae1ec343972141221ea9b243965f 355956 grub2_1.99-14ubuntu2.diff.gz
Debian-Vcs-Browser: http://anonscm.debian.org/loggerhead/pkg-grub/trunk/grub/
Debian-Vcs-Bzr: http://anonscm.debian.org/bzr/pkg-grub/trunk/grub/
Original-Maintainer: GRUB Maintainers <pkg-grub-devel@lists.alioth.debian.org>

-----BEGIN PGP SIGNATURE-----
Version: GnuPG v1.4.11 (GNU/Linux)
Comment: Colin Watson <cjwatson@debian.org> -- Debian developer

iQIVAwUBTtLmAjk1h9l9hlALAQiB4BAAk2SJKs3TIeOJ0tgQffvybm22P0dJudn9
Vzxf7lQdw5/VC8wfcjdwBMTgkb+l4yyLCxtntjCkgCdIbaJEEDbC8loAtLomSSfl
IPaul3rLctHHNbo1bs+/ghM4n2+dT2MXm3XPeX+QnM1tvt8dpTM/cxSG0MJJz3+4
EZUwU/1As7hraJ4u2eEnoeCXFPSykiCMOJO+XVpvtKaizzuo1rqMxlLa+LPjSJrd
Zr+Q2CjuAy+nuq/YNdaXKSaSTe6c5Ot4zPSSjGTpZJtlwxIwTxkm/uv9weBOFFCE
Yw1WtYoM7hgBrXsDzCfjCiDF1c/khhdSYRV4K4oPNh0GmKvgBe3Wk9ClNqWelLpZ
oEwyEYffIblj61s8xuMqshawLnGEQQI3Vp0x3mj6qkUeiL+eqdgP5vHZe+Ja2tob
ZdvbdbjlYRMdzGLbMmRxo4SFwTlAsD8J5GPdEIYb8/5TEFDVQlzzEsIhIBvDQ9MQ
YPiKc0Cqk6qJDAWhLLKBqL0T6GcWph/TQrcZjuKizFbd16LlW6JQV4fJYkIflqaj
CJlZm5aPC/83DpSdox2wcp9ky0hLMyOjOsWHZ2dUKMdkNWIfHO/LBtKEXvyfW+pY
b78JWeNaQ2IClT8hrTnGcVLFep+qKHNQOChThOYC+K8tI3pxWYtvhCI3+wOCSD9R
ToHTdL+dwao=
=8M5G
-----END PGP SIGNATURE-----