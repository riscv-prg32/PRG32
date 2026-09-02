import argparse
import socket
import sys
import time

try:
    import pyaudio
except ImportError:
    print("PyAudio not found. Please run: pip install pyaudio")
    sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="PRG32 QEMU Audio Player")
    parser.add_argument("url", help="Socket URL", nargs='?', default="socket://127.0.0.1:4321")
    parser.add_argument("--rate", type=int, default=22050, help="Sample rate")
    parser.add_argument("--channels", type=int, default=1, help="Channels")
    args = parser.parse_args()

    if not args.url.startswith("socket://"):
        print("Only socket:// URLs are supported for the QEMU Audio Player")
        sys.exit(1)

    host_port = args.url[9:].split(':')
    host = host_port[0]
    port = int(host_port[1])

    p = pyaudio.PyAudio()

    # CRITICAL: Force PyAudio to open a 44100 Hz Stereo stream.
    # Mac CoreAudio often struggles with 22050 Hz Mono and changes the pitch.
    # We will manually upsample the QEMU data to match this robust format.
    stream = p.open(format=pyaudio.paInt16,
                    channels=2,
                    rate=44100,
                    output=True)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    # CRITICAL: Disable Nagle's Algorithm!
    # Nagle's algorithm delays small packets (like our 1-byte ACKs and 882-byte chunks)
    # by up to 200ms to batch them. This causes massive stuttering. Disabling it
    # guarantees instant transmission of our flow control ACKs.
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    print(f"Waiting for QEMU Audio Stream at {host}:{port}...")
    while True:
        try:
            sock.connect((host, port))
            break
        except ConnectionRefusedError:
            time.sleep(0.25)

    print(f"Connected! Playing {args.rate}Hz {args.channels}-channel audio...")

    try:
        qemu_channels = args.channels
        frame_size = 2 * qemu_channels

        # Send 10 initial credits to build a 200ms jitter buffer (crucial for Linux PulseAudio)
        sock.sendall(b'KKKKKKKKKK')

        import array

        while True:
            # QEMU sends exactly 441 frames per chunk.
            # We must demand EXACTLY this amount, otherwise MSG_WAITALL will block
            # waiting for the next QEMU chunk, artificially starving PyAudio!
            chunk_size = 441 * frame_size
            data = sock.recv(chunk_size, socket.MSG_WAITALL)
            if not data:
                print("Stream disconnected.")
                break

            # Send 1 credit back to QEMU BEFORE blocking on playback.
            # This pipelines QEMU's chunk generation with our host playback, hiding network latency.
            sock.sendall(b'K')

            # Manually upsample the 22050 Hz QEMU stream to 44100 Hz Stereo for PyAudio
            in_arr = array.array('h', data)
            if qemu_channels == 1:
                # Mono: repeat each sample 4 times (Left, Right, Left, Right)
                out_arr = array.array('h', (s for s in in_arr for _ in range(4)))
            else:
                # Stereo: repeat each L/R pair twice
                out_arr = array.array('h')
                for i in range(0, len(in_arr), 2):
                    out_arr.extend((in_arr[i], in_arr[i+1], in_arr[i], in_arr[i+1]))

            # Stream directly to host audio. This blocking call paces the entire system!
            try:
                stream.write(out_arr.tobytes(), exception_on_underflow=False)
            except OSError as e:
                # Catch random ALSA/PulseAudio underflow errors on Linux to prevent crashes
                pass

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        sock.close()
        stream.stop_stream()
        stream.close()
        p.terminate()

if __name__ == "__main__":
    main()
