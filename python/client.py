import socket
import struct
import threading
import time
import queue

import av
import cv2
import numpy as np


SERVER_IP = '127.0.0.1'
VIDEO_PORT = 12345
CONTROL_PORT = 12346

latest_frame = None
running = True
control_socket = None
control_connected = False

# Thread-safe queue for mouse events (maxsize prevents memory leak if server is down)
mouse_queue = queue.Queue(maxsize=100)

def recv_exactly(sock, n):
    buf = bytearray()
    while len(buf) < n:
        try:
            chunk = sock.recv(n - len(buf))
        except (ConnectionResetError, OSError):
            return None
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)

def video_receiver():
    global latest_frame, running

    while running:
        video_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            video_socket.connect((SERVER_IP, VIDEO_PORT))
            print("Video: Connected to server.")
            codec = av.CodecContext.create("h264", "r")

            while running:
                header = recv_exactly(video_socket, 4)
                if not header:
                    break
                size = struct.unpack('>I', header)[0]
                packet_data = recv_exactly(video_socket, size)
                if not packet_data:
                    break

                packets = codec.parse(packet_data)
                for packet in packets:
                    try:
                        frames = codec.decode(packet)
                        for frame in frames:
                            latest_frame = frame.to_ndarray(format='bgr24')
                    except Exception:
                        pass

        except ConnectionRefusedError:
            print("Video: Server not available. Retrying in 1s...")
        except Exception as e:
            print(f"Video: Error - {e}")
        finally:
            video_socket.close()

        if running:
            latest_frame = None
            time.sleep(1.0)

def control_sender():
    global running, control_socket, control_connected

    while running:
        if not control_connected:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect((SERVER_IP, CONTROL_PORT))
                control_socket = sock
                control_connected = True
                print("Control: Connected to server.")
            except ConnectionRefusedError:
                if control_socket:
                    control_socket.close()
                control_socket = None
                time.sleep(1.0)
                continue
            except Exception as e:
                print(f"Control: Error - {e}")
                time.sleep(1.0)
                continue

        # If we are here, we are connected
        try:
            # Get mouse data from queue (blocks up to 0.1s)
            ev_type, x, y = mouse_queue.get(timeout=0.1)
            control_socket.sendall(struct.pack('>Bii', ev_type, x, y))
        except queue.Empty:
            pass
        except (ConnectionResetError, BrokenPipeError, OSError):
            print("Control: Disconnected.")
            control_connected = False
            if control_socket:
                control_socket.close()
                control_socket = None

# Mouse state
EV_DOWN = 0
EV_MOVE = 1
EV_UP   = 2

def on_mouse(event, x, y, flags, param):
    ev = None
    if event == cv2.EVENT_LBUTTONDOWN:
        ev = EV_DOWN
    elif event == cv2.EVENT_MOUSEMOVE:
        ev = EV_MOVE
    elif event == cv2.EVENT_LBUTTONUP:
        ev = EV_UP
    if ev is None:
        return
    try:
        mouse_queue.put_nowait((ev, int(x), int(y)))
    except queue.Full:
        pass

def main():
    global latest_frame, running

    # Start background threads
    t_video = threading.Thread(target=video_receiver, daemon=True)
    t_video.start()

    t_control = threading.Thread(target=control_sender, daemon=True)
    t_control.start()

    cv2.namedWindow('CAD Stream')
    cv2.setMouseCallback('CAD Stream', on_mouse)

    while running:
        # 1. Check if user clicked the 'X' button.
        try:
            if cv2.getWindowProperty('CAD Stream', cv2.WND_PROP_VISIBLE) < 1:
                break
        except cv2.error:
            break

        # 2. Draw the frame
        if latest_frame is not None:
            cv2.imshow('CAD Stream', latest_frame)
        else:
            placeholder = np.zeros((600, 800, 3), dtype=np.uint8)
            cv2.putText(placeholder, "Waiting for server...", (230, 300),
                        cv2.FONT_HERSHEY_SIMPLEX, 1.0, (255, 255, 255), 2)
            cv2.imshow('CAD Stream', placeholder)

        # 3. Process keyboard events (this keeps the window responsive)
        key = cv2.waitKey(10) & 0xFF
        if key == ord('q'):
            break

    # Cleanup
    running = False
    time.sleep(0.1)  # Give threads a moment to exit their loops

    if control_socket:
        control_socket.close()
    cv2.destroyAllWindows()

if __name__ == '__main__':
    main()
