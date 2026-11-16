import socket
import struct
import threading
import time
import queue

import av
import numpy as np
import pygame


SERVER_IP = "127.0.0.1"
VIDEO_PORT = 12345
CONTROL_PORT = 12346

WINDOW_NAME = "CAD Stream"
INITIAL_W = 1024
INITIAL_H = 768
FPS = 60

# Read by control_sender after (re)connect; written by the main thread
# on VIDEORESIZE. Tuple assignment is atomic under CPython, so the
# control thread either sees the old or the new size, never a torn one.
current_window_size = (INITIAL_W, INITIAL_H)

latest_frame = None
running = True
control_socket = None
control_connected = False

EV_MOUSE_DOWN = 0
EV_MOUSE_MOVE = 1
EV_MOUSE_UP   = 2
EV_KEY_DOWN   = 3
EV_KEY_UP     = 4
EV_RESIZE = 5

# Thread-safe queue for input events (mouse + keyboard)
event_queue = queue.Queue(maxsize=200)

# pygame key constants -> canonical X11 keysyms the server expects.
# Letters, digits and space already match ASCII: pygame.K_w == 119 == 'w',
# so they need no entry and pass through as-is
KEY_MAP = {
    pygame.K_LEFT:      0xFF51,
    pygame.K_UP:        0xFF52,
    pygame.K_RIGHT:     0xFF53,
    pygame.K_DOWN:      0xFF54,
    pygame.K_LSHIFT:    0xFFE1,
    pygame.K_RSHIFT:    0xFFE2,
    pygame.K_LCTRL:     0xFFE3,
    pygame.K_RCTRL:     0xFFE4,
    pygame.K_LALT:      0xFFE9,
    pygame.K_RALT:      0xFFEA,
    pygame.K_TAB:       0xFF09,
    pygame.K_RETURN:    0xFF0D,
    pygame.K_ESCAPE:    0xFF1B,
    pygame.K_BACKSPACE: 0xFF08,
}


def key_to_scancode(key):
    if key in KEY_MAP:
        return KEY_MAP[key]
    if 0 <= key <= 0xFFFF:
        return key
    return None


def pack_event(ev, a, b):
    if ev in (EV_MOUSE_DOWN, EV_MOUSE_MOVE, EV_MOUSE_UP):
        return struct.pack('>Bii', ev, a, b)
    if ev == EV_RESIZE:
        return struct.pack('>BHH', ev, a & 0xFFFF, b & 0xFFFF)
    return struct.pack('>BH', ev, a & 0xFFFF)


def video_receiver():
    global latest_frame, running

    while running:
        video_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        stream = None
        try:
            video_socket.connect((SERVER_IP, VIDEO_PORT))
            print("Video: Connected to server.")
            codec = av.CodecContext.create("h264", "r")
            stream = video_socket.makefile('rb')

            while running:
                header = stream.read(4)
                if len(header) < 4:
                    break
                size = struct.unpack('>I', header)[0]
                packet_data = stream.read(size)
                if len(packet_data) < size:
                    break

                packets = codec.parse(packet_data)
                for packet in packets:
                    try:
                        for frame in codec.decode(packet):
                            latest_frame = np.ascontiguousarray(frame.to_ndarray(format='bgr24'))
                    except Exception:
                        pass
        except ConnectionRefusedError:
            print("Video: Server not available. Retrying in 1s...")
        except Exception as e:
            print(f"Video: Error - {e}")
        finally:
            if stream is not None:
                stream.close()
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
                w, h = current_window_size
                sock.sendall(pack_event(EV_RESIZE, w, h))
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

        try:
            ev, a, b = event_queue.get(timeout=0.1)
            control_socket.sendall(pack_event(ev, a, b))
        except queue.Empty:
            pass
        except (ConnectionResetError, BrokenPipeError, OSError):
            print("Control: Disconnected.")
            control_connected = False
            if control_socket:
                control_socket.close()
                control_socket = None


def main():
    global latest_frame, running, current_window_size

    pygame.init()
    screen = pygame.display.set_mode((INITIAL_W, INITIAL_H), pygame.RESIZABLE)
    pygame.display.set_caption(WINDOW_NAME)
    clock = pygame.time.Clock()
    font = pygame.font.SysFont('sans', 24)
    placeholder = font.render("Waiting for server...", True, (255, 255, 255))

    t_video = threading.Thread(target=video_receiver, daemon=True)
    t_video.start()
    t_control = threading.Thread(target=control_sender, daemon=True)
    t_control.start()

    mouse_down = False
    pressed_keys = set()

    try:
        while running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                    break

                elif event.type in (pygame.VIDEORESIZE, pygame.WINDOWSIZECHANGED):
                    # It only works when you release the mouse :/
                    pass

                elif event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        running = False
                        break
                    sc = key_to_scancode(event.key)
                    if sc is not None and sc not in pressed_keys:
                        pressed_keys.add(sc)
                        try:
                            event_queue.put_nowait((EV_KEY_DOWN, sc, 0))
                        except queue.Full:
                            pass

                elif event.type == pygame.KEYUP:
                    sc = key_to_scancode(event.key)
                    if sc is not None:
                        pressed_keys.discard(sc)
                        try:
                            event_queue.put_nowait((EV_KEY_UP, sc, 0))
                        except queue.Full:
                            pass

                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    mouse_down = True
                    try:
                        event_queue.put_nowait((EV_MOUSE_DOWN, event.pos[0], event.pos[1]))
                    except queue.Full:
                        pass

                elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
                    mouse_down = False
                    try:
                        event_queue.put_nowait((EV_MOUSE_UP, event.pos[0], event.pos[1]))
                    except queue.Full:
                        pass

                elif event.type == pygame.MOUSEMOTION and mouse_down:
                    try:
                        event_queue.put_nowait((EV_MOUSE_MOVE, event.pos[0], event.pos[1]))
                    except queue.Full:
                        pass

            # H.264 4:2:0 requires even dimensions; snap to even so the server
            # and the encoder agree with the window we create here
            win_w, win_h = screen.get_size()
            win_w &= ~1
            win_h &= ~1
            if (win_w, win_h) != current_window_size:
                current_window_size = (win_w, win_h)
                screen = pygame.display.set_mode((win_w, win_h), pygame.RESIZABLE)
                try:
                    event_queue.put_nowait((EV_RESIZE, win_w, win_h))
                except queue.Full:
                    pass

            if latest_frame is not None:
                h, w = latest_frame.shape[:2]
                surf = pygame.image.frombuffer(latest_frame, (w, h), 'BGR')
                screen.blit(surf, (0, 0))
            else:
                w, h = screen.get_size()
                screen.fill((0, 0, 0))
                screen.blit(placeholder,
                            ((w - placeholder.get_width())  // 2,
                             (h - placeholder.get_height()) // 2))

            pygame.display.flip()
            clock.tick(FPS)

    finally:
        running = False
        time.sleep(0.05)  # let control thread drain the queue
        if control_socket:
            try:
                for sc in list(pressed_keys):
                    control_socket.sendall(pack_event(EV_KEY_UP, sc, 0))
            except Exception:
                pass
            control_socket.close()
        pygame.quit()


if __name__ == "__main__":
    main()
