# This python script listens on UDP port 3333 
# for messages from the Wio Terminal board and prints them
import socket
import sys
import time

try :
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
except OSError as e:
    print('Failed to create socket:', e)
    sys.exit(1)

try:
    s.bind(('', 3333))
except OSError as e:
    print('Bind failed:', e)
    sys.exit(1)
     
print('Server listening')

# Collect 10 consecutive packets based on SEQ field
expected_seq = None
received_times = []  # wall-clock arrival times (seconds)
received_seqs = []

while True:
    d = s.recvfrom(1024)
    data = d[0]
    recv_ts = time.time()

    if not data:
        continue

    try:
        msg = data.strip().decode('utf-8', errors='ignore')
        # Expect format: SEQ=123,T=456789
        parts = dict(p.split('=') for p in msg.split(',') if '=' in p)
        seq = int(parts.get('SEQ'))
    except Exception:
        # Ignore malformed packet
        continue

    if expected_seq is None:
        expected_seq = seq
        received_times = [recv_ts]
        received_seqs = [seq]
        continue

    # Enforce consecutive packets: if gap, reset collection
    if seq != expected_seq + 1:
        expected_seq = seq
        received_times = [recv_ts]
        received_seqs = [seq]
        continue

    expected_seq = seq
    received_times.append(recv_ts)
    received_seqs.append(seq)

    if len(received_times) == 10:
        # Compute inter-arrival times between successive packets (9 intervals)
        intervals = [ (received_times[i] - received_times[i-1]) for i in range(1, len(received_times)) ]
        avg_interval = sum(intervals) / len(intervals) if intervals else 0.0
        print('Received 10 consecutive packets: {}-{}'.format(received_seqs[0], received_seqs[-1]))
        print('Average inter-arrival time over 10 packets: {:.3f} ms'.format(avg_interval * 1000.0))
        break

s.close()