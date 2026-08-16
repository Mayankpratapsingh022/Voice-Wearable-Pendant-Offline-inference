#!/usr/bin/env bash
set -euo pipefail

: "${IDF_PATH:?Source ESP-IDF export.sh before running this script.}"

patch_file="$(cd "$(dirname "$0")/.." && pwd)/patches/esp-idf-v5.5-lcd-psram-dma.patch"
target="$IDF_PATH/components/esp_lcd/spi/esp_lcd_panel_io_spi.c"

if rg -q 'SPI_TRANS_DMA_USE_PSRAM' "$target"; then
  echo "ESP-IDF LCD PSRAM-DMA fix is already applied."
  exit 0
fi

patch -d "$IDF_PATH" -p1 -N < "$patch_file"
