#
# Copyright 2020 Ettus Research, a National Instruments Brand
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
"""This module houses the ChdrEndpoint class, which handles networking,
packet dispatch, and acts as an interface between these and the RFNoC
Graph.
"""

from threading import Thread
import socket
import queue
import select
from uhd.chdr import ChdrPacket, ChdrWidth
from .rfnoc_graph import XbarNode, XportNode, StreamEndpointNode, RFNoCGraph, NodeType
from .chdr_stream import SendWrapper, ChdrOutputStream, ChdrInputStream, SelectableQueue

CHDR_W = ChdrWidth.W64

class ChdrEndpoint:
    """This class is created by the sim periph_manager
    It is responsible for opening sockets, dispatching all chdr packet
    traffic to the appropriate destination, and responding to said
    traffic.

    The config parameter is a Config object (see simulator/config.py)
    """
    def __init__(self, log, config):
        self.log = log.getChild("ChdrEndpoint")
        self.config = config
        self.source_gen = config.source_gen
        self.sink_gen = config.sink_gen
        self.xport_map = {}

        self.send_queue = SelectableQueue()
        self.send_wrapper = SendWrapper(self.send_queue)

        self.graph = RFNoCGraph(self.get_default_nodes(), self.log, 1, self.send_wrapper, CHDR_W)
        self.thread = Thread(target=self.socket_worker, daemon=True)
        self.thread.start()

    def set_device_id(self, device_id):
        """Set the device_id for this endpoint"""
        self.graph.set_device_id(device_id)

    def set_sample_rate(self, rate):
        """Set the sample_rate of the next tx_stream.

        This method is called by the daughterboard. It coresponds to
        sim_dboard.py:sim_db#set_catalina_clock_rate()
        """
        self.graph.get_stream_spec().sample_rate = rate

    def get_default_nodes(self):
        """Get a sensible NoC Core setup. This is the simplest
        functional layout. It has one of each required component.
        """
        nodes = [
            XportNode(0),
            XbarNode(0, [2], [0]),
            StreamEndpointNode(0, self.source_gen, self.sink_gen)
        ]
        return nodes

    def send_strc(self, stream_ep, addr):
        pass # TODO: currently not implemented

    def begin_tx(self, src_epid, stream_spec):
        pass # TODO: currently not implemented

    def end_tx(self, src_epid):
        pass # TODO: currently not implemented

    def begin_rx(self, dst_epid):
        pass # TODO: currently not implemented

    def socket_worker(self):
        """This is the method that runs in a background thread. It
        blocks on the CHDR socket and processes packets as they come
        in.
        """
        self.log.info("Starting ChdrEndpoint Thread")
        main_sock = socket.socket(socket.AF_INET,
                                  socket.SOCK_DGRAM)
        main_sock.bind(("0.0.0.0", 49153))

        while True:
            # This allows us to block on multiple sockets at the same time
            ready_list, _, _ = select.select([main_sock, self.send_queue], [], [])
            buffer = bytearray(8000) # Max MTU
            for sock in ready_list:
                if sock is main_sock:
                    # Received Data over socket
                    n_bytes, sender = main_sock.recvfrom_into(buffer)
                    self.log.trace("Received {} bytes of data from {}"
                                   .format(n_bytes, sender))
                    try:
                        packet = ChdrPacket.deserialize(CHDR_W, buffer[:n_bytes])
                        self.log.trace("Decoded Packet: {}"
                                       .format(packet.to_string_with_payload()))
                        entry_xport = (NodeType.XPORT, 0)
                        response = self.graph.handle_packet(packet, entry_xport, sender,
                                                            sender, n_bytes)

                        if response is not None:
                            data = response.serialize()
                            self.log.trace("Returning Packet: {}"
                                           .format(packet.to_string_with_payload()))
                            main_sock.sendto(bytes(data), sender)
                    except BaseException as ex:
                        self.log.warning("Unable to decode packet: {}"
                                         .format(ex))
                        raise ex
                else:
                    data, addr = self.send_queue.get()
                    sent_len = main_sock.sendto(data, addr)
                    assert len(data) == sent_len, "Didn't send whole packet."