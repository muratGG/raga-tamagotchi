#!/bin/sh
# Builds the Rockbox imx tools this project uses into tools/bin:
#   sbtoelf   unpack a firmware image (.sb)
#   elftosb   pack one
#   sbloader  send one to a device in USB recovery mode
#   scsitool  read/write the firmware of a device in normal USB mode:
#             native on Linux; on macOS over libusb (tools/rbscsi-libusb.c,
#             run with sudo), plus Windows .exe builds via zig if present
#
# Needs: git, make, a C compiler, pkg-config + libusb-1.0; zig (optional) for
# the Windows scsitool. The Rockbox sources are GPLv2 and pinned below.
set -eu

ROCKBOX_URL=https://github.com/Rockbox/rockbox.git
ROCKBOX_COMMIT=fc3db7a1f49f0c231e93937413ba63785bb4ad62

here=$(cd "$(dirname "$0")" && pwd)
src=$here/src/rockbox
bin=$here/bin
mkdir -p "$bin"

if [ ! -d "$src/.git" ]; then
    git clone --filter=blob:none --no-checkout "$ROCKBOX_URL" "$src"
    git -C "$src" sparse-checkout set utils/imxtools utils/scsi utils/tomcrypt
fi
git -C "$src" checkout -q "$ROCKBOX_COMMIT"

make -C "$src/utils/imxtools/sbtools" sbtoelf elftosb sbloader
cp "$src/utils/imxtools/sbtools/sbtoelf" \
   "$src/utils/imxtools/sbtools/elftosb" \
   "$src/utils/imxtools/sbtools/sbloader" "$bin/"

scsi_src="$src/utils/imxtools/scsitools/scsitool.c $src/utils/imxtools/scsitools/misc.c \
          $src/utils/imxtools/scsitools/stmp_scsi.c $src/utils/scsi/rbscsi.c"
case "$(uname -s)" in
Linux)
    # shellcheck disable=SC2086
    cc -std=gnu99 -O1 -I"$src/utils/scsi" -o "$bin/scsitool" $scsi_src
    ;;
*)
    # rbscsi has no backend here; ours speaks USB mass storage via libusb
    scsi_tool_src=$(echo "$scsi_src" | sed "s|$src/utils/scsi/rbscsi.c|$here/rbscsi-libusb.c|")
    # shellcheck disable=SC2086
    cc -std=gnu99 -O1 -I"$src/utils/scsi" $(pkg-config --cflags libusb-1.0) \
        -o "$bin/scsitool" $scsi_tool_src $(pkg-config --libs libusb-1.0)
    if command -v zig >/dev/null; then
        for arch in aarch64 x86_64; do
            # shellcheck disable=SC2086
            zig cc -target $arch-windows-gnu -std=gnu99 -O1 -I"$src/utils/scsi" \
                -o "$bin/scsitool-$arch.exe" $scsi_src
        done
        rm -f "$bin"/*.pdb
    else
        echo "zig not found: skipping the Windows scsitool (prebuilt/ has copies)"
    fi
    ;;
esac

echo "tools in $bin:"
ls "$bin"
