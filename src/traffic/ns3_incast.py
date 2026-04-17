"""Run an ns3 TCP incast simulation and stream packet arrivals to a C++ queue engine.

Usage:
    python -m traffic.ns3_incast --scenario low --host 127.0.0.1 --port 9000

ns3 models real TCP congestion control (NewReno by default), so packet timing and
sizes reflect actual TCP dynamics — slow start, congestion avoidance, retransmits —
rather than the synthetic fixed-size bursts of the Python generator.

The simulation runs to completion first, then replays the collected packet arrivals
over the socket at their original inter-packet timing (same replay pattern as
socket_sender.py). The C++ engine's SocketPacketSource stamps each received packet
with wall-clock time on arrival.
"""
from __future__ import annotations

import argparse
import ctypes
import glob as globmod
import os
import socket
import struct
import sys
import tempfile
import time
from typing import Iterator

from .config import ScenarioName, TrafficConfig, get_scenario


# ---------------------------------------------------------------------------
# ns3 loader
# ---------------------------------------------------------------------------

def _load_ns3():
    """Pre-load ns3 shared libraries then import the Python bindings."""
    ns3_lib = os.path.join(
        os.path.dirname(sys.executable),
        "..",
        "lib/python{}.{}/dist-packages/ns3/lib64".format(*sys.version_info[:2]),
    )
    # Fallback: scan site-packages
    for path in sys.path:
        candidate = os.path.join(path, "ns3", "lib64")
        if os.path.isdir(candidate):
            ns3_lib = candidate
            break

    # Some libs have unsatisfied GTK / LTE deps — skip them gracefully
    _skip = {"config-store", "visualizer", "click", "tap-bridge", "emu", "lte", "netanim"}
    for lib in sorted(globmod.glob(os.path.join(ns3_lib, "libns3*.so"))):
        if any(s in lib for s in _skip):
            continue
        try:
            ctypes.CDLL(lib, ctypes.RTLD_GLOBAL)
        except OSError:
            pass

    ns3_pkg = os.path.dirname(ns3_lib)
    if ns3_pkg not in sys.path:
        sys.path.insert(0, ns3_pkg)

    from ns import ns  # noqa: PLC0415
    return ns


# ---------------------------------------------------------------------------
# PCAP parser
# ---------------------------------------------------------------------------

def _iter_pcap_tcp_data(path: str) -> Iterator[tuple[int, int, str]]:
    """Yield (timestamp_us, ip_total_bytes, src_ip) for TCP segments carrying data.

    Handles both Ethernet (DLT=1) and PPP (DLT=9) link layers.
    ns3 PointToPoint devices use PPP encapsulation.
    """
    with open(path, "rb") as f:
        header = f.read(24)
        if len(header) < 24:
            return
        magic, _vmaj, _vmin, _zone, _sigfigs, _snaplen, link_type = struct.unpack_from(
            "<IHHiIII", header
        )
        # 0xa1b2c3d4 = microsecond timestamps, 0xa1b23c4d = nanosecond
        ts_divisor = 1000 if magic == 0xa1b23c4d else 1

        while True:
            rec = f.read(16)
            if len(rec) < 16:
                break
            ts_sec, ts_sub, incl_len, _orig_len = struct.unpack_from("<IIII", rec)
            frame = f.read(incl_len)
            ts_us = ts_sec * 1_000_000 + ts_sub // ts_divisor

            # Determine where IP header starts based on link layer type
            if link_type == 1:
                # Ethernet: 14-byte header, check EtherType
                if len(frame) < 34:
                    continue
                if struct.unpack_from("!H", frame, 12)[0] != 0x0800:
                    continue
                ip_off = 14
            elif link_type == 9:
                # PPP: 2-byte protocol field (0x0021 = IPv4)
                if len(frame) < 4:
                    continue
                if struct.unpack_from("!H", frame, 0)[0] != 0x0021:
                    continue
                ip_off = 2
            else:
                continue

            ip_ihl = (frame[ip_off] & 0x0F) * 4
            ip_total = struct.unpack_from("!H", frame, ip_off + 2)[0]
            ip_proto = frame[ip_off + 9]
            src_ip = socket.inet_ntoa(frame[ip_off + 12 : ip_off + 16])

            if ip_proto != 6:  # TCP only
                continue

            tcp_off = ip_off + ip_ihl
            if tcp_off + 13 >= len(frame):
                continue
            tcp_data_offset = ((frame[tcp_off + 12] >> 4) * 4)
            tcp_payload_len = ip_total - ip_ihl - tcp_data_offset

            if tcp_payload_len <= 0:  # skip SYN / ACK-only / FIN
                continue

            yield ts_us, ip_total, src_ip


# ---------------------------------------------------------------------------
# Simulation
# ---------------------------------------------------------------------------

def _simulate(
    config: TrafficConfig,
    link_bps: int,
    buffer_bytes: int,
) -> tuple[list[tuple[int, int, str]], set[str]]:
    """Build and run the incast topology, return collected packets and control IPs."""
    ns = _load_ns3()

    n = config.senders_per_wave
    # nodes: 0..n-1 = senders, n = aggregator, n+1 = receiver
    all_nodes = ns.NodeContainer()
    all_nodes.Create(n + 2)
    agg = all_nodes.Get(n)
    rcvr = all_nodes.Get(n + 1)

    stack = ns.InternetStackHelper()
    stack.InstallAll()

    # Access links: sender_i ↔ aggregator (10 Gbps, 1 µs — not the bottleneck)
    access = ns.PointToPointHelper()
    access.SetDeviceAttribute("DataRate", ns.StringValue("10Gbps"))
    access.SetChannelAttribute("Delay", ns.StringValue("1us"))

    addr_helper = ns.Ipv4AddressHelper()
    sender_ips: dict[int, str] = {}

    for i in range(n):
        nc = ns.NodeContainer()
        nc.Add(all_nodes.Get(i))
        nc.Add(agg)
        devs = access.Install(nc)

        # Unique /24 subnet per link: 10.{1+i//254}.{1+i%254}.0
        subnet = "10.{}.{}.0".format(1 + i // 254, 1 + i % 254)
        addr_helper.SetBase(ns.Ipv4Address(subnet), ns.Ipv4Mask("255.255.255.0"))
        ifaces = addr_helper.Assign(devs)
        sender_ips[i] = str(ifaces.GetAddress(0, 0))

    # Bottleneck: aggregator ↔ receiver
    bottle = ns.PointToPointHelper()
    bottle.SetDeviceAttribute("DataRate", ns.StringValue(f"{link_bps}bps"))
    bottle.SetChannelAttribute("Delay", ns.StringValue("1us"))
    bottle.SetQueue(
        "ns3::DropTailQueue",
        "MaxSize",
        ns.StringValue(f"{buffer_bytes}B"),
    )

    bottle_nc = ns.NodeContainer()
    bottle_nc.Add(agg)
    bottle_nc.Add(rcvr)
    bottle_devs = bottle.Install(bottle_nc)

    addr_helper.SetBase(ns.Ipv4Address("10.0.0.0"), ns.Ipv4Mask("255.255.255.252"))
    bottle_ifaces = addr_helper.Assign(bottle_devs)
    rcvr_ip = bottle_ifaces.GetAddress(1, 0)

    ns.Ipv4GlobalRoutingHelper.PopulateRoutingTables()

    # Simulation end: last wave start + enough time for TCP to drain
    sim_end_us = (
        config.first_wave_start_us
        + config.number_of_waves * config.wave_interval_us
        + 500_000  # 500 ms drain time
    )
    sim_end_s = sim_end_us / 1e6

    # PacketSink on receiver
    any_addr = ns.InetSocketAddress(ns.Ipv4Address.GetAny(), 9).ConvertTo()
    sink_helper = ns.PacketSinkHelper("ns3::TcpSocketFactory", any_addr)
    sink_apps = sink_helper.Install(rcvr)
    sink_apps.Start(ns.Seconds(0.0))
    sink_apps.Stop(ns.Seconds(sim_end_s + 1.0))

    # BulkSend per sender per wave with per-sender jitter
    import random  # noqa: PLC0415
    rng = random.Random(config.seed)
    rcvr_addr = ns.InetSocketAddress(rcvr_ip, 9).ConvertTo()

    control_sender_ids = set(range(0, n, config.control_packet_every_n))
    control_ips = {sender_ips[i] for i in control_sender_ids}

    for w in range(config.number_of_waves):
        wave_base_us = config.first_wave_start_us + w * config.wave_interval_us
        for i in range(n):
            jitter_us = rng.randint(0, config.max_start_offset_us)
            start_us = wave_base_us + jitter_us

            bulk = ns.BulkSendHelper("ns3::TcpSocketFactory", rcvr_addr)
            bulk.SetAttribute(
                "MaxBytes", ns.UintegerValue(config.bytes_per_sender_per_wave)
            )
            apps = bulk.Install(all_nodes.Get(i))
            apps.Start(ns.MicroSeconds(start_us))
            apps.Stop(ns.Seconds(sim_end_s + 1.0))

    # Capture packets arriving at receiver via the bottleneck link
    tmpdir = tempfile.mkdtemp(prefix="ns3_incast_")
    rcvr_nc = ns.NodeContainer()
    rcvr_nc.Add(rcvr)
    bottle.EnablePcap(os.path.join(tmpdir, "rx"), rcvr_nc)

    print(
        f"Running ns3 simulation: {n} senders × {config.number_of_waves} waves "
        f"({sim_end_s:.1f}s sim time) …"
    )
    ns.Simulator.Stop(ns.Seconds(sim_end_s + 1.0))
    ns.Simulator.Run()
    ns.Simulator.Destroy()

    # Parse PCAP
    packets: list[tuple[int, int, str]] = []
    for pcap in globmod.glob(os.path.join(tmpdir, "rx*.pcap")):
        packets.extend(_iter_pcap_tcp_data(pcap))
    packets.sort()

    print(f"Simulation done: {len(packets)} TCP data packets captured.")
    return packets, control_ips


# ---------------------------------------------------------------------------
# Socket sender
# ---------------------------------------------------------------------------

def send(
    scenario: ScenarioName,
    host: str,
    port: int,
    link_bps: int = 40_000_000_000,
    buffer_bytes: int = 52_428_800,
) -> None:
    config = get_scenario(scenario)
    packets, control_ips = _simulate(config, link_bps, buffer_bytes)

    if not packets:
        print("No packets captured — nothing to send.")
        return

    with socket.create_connection((host, port)) as conn:
        print(f"Connected to {host}:{port} — replaying {len(packets)} packets")
        prev_us = packets[0][0]

        for ts_us, ip_total, src_ip in packets:
            gap_us = ts_us - prev_us
            if gap_us > 0:
                time.sleep(gap_us / 1_000_000)
            prev_us = ts_us

            if src_ip in control_ips:
                tc, tag = "control", config.control_priority_tag
            else:
                tc, tag = "bulk", config.bulk_priority_tag

            conn.sendall(f"{ip_total},{tc},{tag}\n".encode())

    print("All packets sent.")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="ns3 TCP incast sender — streams packet arrivals to a C++ queue engine"
    )
    parser.add_argument(
        "--scenario",
        choices=[s.value for s in ScenarioName],
        default=ScenarioName.LOW.value,
        help="Traffic scenario (default: low)",
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument(
        "--link-bps",
        type=int,
        default=40_000_000_000,
        help="Bottleneck link speed in bps (default: 40 Gbps)",
    )
    parser.add_argument(
        "--buffer-bytes",
        type=int,
        default=52_428_800,
        help="Bottleneck queue buffer in bytes (default: 50 MB)",
    )
    args = parser.parse_args()
    send(
        ScenarioName(args.scenario),
        args.host,
        args.port,
        args.link_bps,
        args.buffer_bytes,
    )


if __name__ == "__main__":
    main()
