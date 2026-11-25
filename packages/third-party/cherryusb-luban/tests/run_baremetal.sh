#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p generated/tests
${CC:-cc} -std=c99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Itests/baremetal -I../cherryusb/common \
  usb_osal_baremetal.c tests/test_baremetal_osal.c -o generated/tests/baremetal-osal
ASAN_OPTIONS=detect_leaks=1 generated/tests/baremetal-osal
python3 prepare_ehci.py ../cherryusb/class/hub/usbh_hub.c \
  patches/0003-hub-baremetal-poll.patch generated/tests/hub.c
${CC:-cc} -std=c99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -DKERNEL_BAREMETAL -ffunction-sections -fdata-sections \
  -Itests/baremetal -Itests/config -I. -I../cherryusb/core \
  -I../cherryusb/common -I../cherryusb/class/hub \
  usb_osal_baremetal.c generated/tests/hub.c tests/test_baremetal_hub.c \
  -Wl,--gc-sections -o generated/tests/baremetal-hub
ASAN_OPTIONS=detect_leaks=1 generated/tests/baremetal-hub
${CC:-cc} -std=c99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -DCONFIG_USB_HS -Itests/baremetal -Itests/config -I. \
  -I../cherryusb/core -I../cherryusb/common -Isdk/class/aic \
  sdk/demo/aicupg_for_usb.c usbd_baremetal.c tests/test_baremetal_device.c \
  -o generated/tests/baremetal-device
ASAN_OPTIONS=detect_leaks=1 generated/tests/baremetal-device
