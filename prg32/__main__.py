import sys
from . import prg32

def main():
    # sys.argv[1:] passes only the actual arguments (like 'qemu' and 'launch')
    sys.exit(prg32.main(sys.argv[1:]))

if __name__ == "__main__":
    main()