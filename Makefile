PORT     ?= /dev/ttyACM1
IDF_SH   := $(HOME)/esp/esp-idf/export.sh

.PHONY: flash clean monitor

# Full clean → build → flash
flash:
	@test -f .env || (echo "ERROR: .env not found. Copy .env.example and fill in values." && exit 1)
	@bash -c 'set -a && source .env && set +a && source $(IDF_SH) && idf.py fullclean && idf.py -p $(PORT) build flash'

# Just monitor (no build/flash)
monitor:
	@bash -c 'source $(IDF_SH) && idf.py -p $(PORT) monitor'

# Delete build artifacts
clean:
	@bash -c 'source $(IDF_SH) && idf.py fullclean'
