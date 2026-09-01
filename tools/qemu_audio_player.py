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
    stream = p.open(format=pyaudio.paInt16,
                    channels=args.channels,
                    rate=args.rate,
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
        frame_size = 2 * args.channels
        
        # Send 2 initial credits to build a tiny 40ms jitter buffer
        sock.sendall(b'KK')
        
        chunks_played = 0
        
        while True:
            # QEMU sends exactly 441 frames per chunk.
            # We must demand EXACTLY this amount, otherwise MSG_WAITALL will block
            # waiting for the next QEMU chunk, artificially starving PyAudio!
            chunk_size = 441 * frame_size
            data = sock.recv(chunk_size, socket.MSG_WAITALL)
            if not data:
                print("Stream disconnected.")
                break
            
            # Stream directly to CoreAudio. This blocking call paces the entire system!
            stream.write(data)
            
            # Send 1 credit back to QEMU to let it generate the next chunk!
            sock.sendall(b'K')
            
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        sock.close()
        stream.stop_stream()
        stream.close()
        p.terminate()

if __name__ == "__main__":
    main()
