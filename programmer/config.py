import os

BAUD        = 2000000
BIN_DIR     = "./programmer/bin"
CHUNK_SIZE  = 65536
FLASH_BASE  = "0x08000000"

# ANSI colours (work on Windows 10+)
C_RESET  = "\033[0m"
C_BOLD   = "\033[1m"
C_CYAN   = "\033[96m"
C_GREEN  = "\033[92m"
C_RED    = "\033[91m"
C_YELLOW = "\033[93m"
C_DIM    = "\033[2m"
C_BLUE   = "\033[94m"
C_MAGENTA= "\033[95m"
C_WHITE   = "\033[97m"
