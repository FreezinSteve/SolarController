# Listen for requests to port 9000 and forward to https://restservice-neon.niwa.co.nz
import socket
import threading
import logging
import time
import ssl

logging.basicConfig(level="DEBUG")

bind_ip = '0.0.0.0'
bind_port = 9000

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind((bind_ip, bind_port))
server.listen(5)  # max backlog of connections

logging.info("Server listening on port %d" % bind_port)


def handle_client_connection(client_socket):
    logging.debug("Handler started")
    neon = None
    start = time.time()
    logging.debug("start get Neon connection")
    neon = get_neon_connection()
    client_socket.settimeout(0.05)
    neon.settimeout(0.05)
    logging.debug("Starting data loop")
    while True:
        try:
            data = client_socket.recv(1024)
            if len(data) > 0:
                logging.debug("RECV CLIENT %s" % data)
                neon.sendall(data)
                start = time.time()
        except socket.timeout as e:
            pass

        try:
            data = neon.recv(1024)
            if len(data) > 0:
                logging.debug("RECV NEON  %s" % data)
                client_socket.sendall(data)
                # '}' only occurs at the end of the REST request and we 
                # need to close the connection for the ESP to detect
                # when it's received all of the data
                if b'}' in data:
                    logging.info("End of data from Neon")
                    time.sleep(1)
                    break
                start = time.time()
        except socket.timeout as e:
            pass

        if time.time() - start > 240:
            logging.info("Socket data timeout")
            break

    # Clean up
    try:
        if neon is not None:
            neon.close()
            logging.debug("Neon socket closed")
        if client_socket is not None:
            client_socket.close()
            logging.debug("Client socket closed")
    except Exception as e:
        logging.error(e)


def get_neon_connection():
    try:
        logging.debug("Connecting to Neon")
        target = "restservice-neon.niwa.co.nz"
        sock = socket.socket(socket.AF_INET)
        context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH)
        # context.options |= ssl.OP_NO_TLSv1 | ssl.OP_NO_TLSv1_1  # optional
        neon_client = context.wrap_socket(sock, server_hostname=target)
        neon_client.connect((target, 443))
        logging.info('Connected to Neon')
        return neon_client
    except Exception as e:
        logging.error(e)


while True:
    client_sock, address = server.accept()
    logging.info('Accepted connection from {}:{}'.format(address[0], address[1]))
    client_handler = threading.Thread(
        target=handle_client_connection,
        args=(client_sock,)
        # without comma you'd get a... TypeError: handle_client_connection() argument after * must be a sequence, not _socketobject
    )
    logging.debug("Starting client handler thread")
    client_handler.start()
