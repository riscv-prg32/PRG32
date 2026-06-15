import sys
from . import prg32

if __name__ == "__main__":
    # sys.argv[1:] passes only the actual arguments (like 'qemu' and 'launch')
    prg32.main(sys.argv[1:])