"""Run an ns3 TCP incast simulation and stream packet arrivals to a C++ queue engine.

Usage:
    python -m traffic.ns3_incast --scenario low --host 127.0.0.1 --port 9000

ns3 models real TCP congestion control (NewReno by default), so packet timing and
sizes reflect actual TCP dynamics — slow start, congestion avoidance, retransmits —
rather than synthetic fixed-size bursts.

Capture point: PCAP on the AGGREGATOR side of the sender→aggregator access links,
BEFORE packets enter the bottleneck DropTailQueue.  This gives the engine the raw
incast burst at ns3 simulation timescale, so the engine's buffer actually fills and
drop policy matters.

Capturing at the receiver (post-bottleneck PCAP) would give traffic already
rate-limited to 45 Gbps; the engine's 45 Gbps link would keep up perfectly and
no drops would occur.

The ns3 simulation timestamp (microseconds) is included in the wire format so
the C++ engine uses it as arrival_time_us instead of wall-clock time.
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

# ns3 PointToPoint PCAP uses DLT_PPP (link type 9).
# ns3 omits the PPP address (0xff) and control (0x03) bytes, so the frame
# is just 2-byte protocol field (0x00 0x21 for IPv4) followed by the IP packet.
_PPP_HDR = 2
_IP_SRC_OFF = 12   # byte offset of source IP inside IP header
_IP_TOT_OFF = 2    # byte offset of total-length field inside IP header
_TCP_DATA_OFF = 32  # minimum IP+TCP header (20+20) — used as floor for payload check


def _iter_pcap_tcp_data(path: str) -> Iterator[tuple[int, int, str]]:
    """Yield (ts_us, ip_total_bytes, src_ip_str) for each TCP data packet in a PCAP."""
    with open(path, "rb") as f:
        magic = struct.unpack_from("<I", f.read(4))[0]
        if magic == 0xA1B2C3D4:
            endian = "<"
        elif magic == 0xD4C3B2A1:
            endian = ">"
        else:
            return  # unrecognised magic — skip file

        # global header: magic(4) ver_maj(2) ver_min(2) thiszone(4) sigfigs(4) snaplen(4) network(4)
        f.seek(24)

        while True:
            rec_hdr = f.read(16)
            if len(rec_hdr) < 16:
                break
            ts_sec, ts_usec, incl_len, _ = struct.unpack_from(endian + "IIII", rec_hdr)
            payload = f.read(incl_len)
            if len(payload) < incl_len:
                break

            ts_us = ts_sec * 1_000_000 + ts_usec

            # Strip PPP header
            if len(payload) < _PPP_HDR:
                continue
            ip = payload[_PPP_HDR:]

            if len(ip) < 20:
                continue  # too short for IP header

            ip_total = struct.unpack_from("!H", ip, _IP_TOT_OFF)[0]
            if ip_total <= _TCP_DATA_OFF:
                continue  # no TCP payload (SYN/ACK/FIN)

            protocol = ip[9]
            if protocol != 6:
                continue  # not TCP

            src_bytes = ip[_IP_SRC_OFF:_IP_SRC_OFF + 4]
            src_ip = "{}.{}.{}.{}".format(*src_bytes)

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

    # Capture on the AGGREGATOR side of each sender→aggregator access link.
    # These PCAPs fire BEFORE the bottleneck DropTailQueue, so the engine sees
    # the raw incast burst.  ns3 PCAP timestamps are simulation-time microseconds,
    # used as arrival_time_us in the wire format so the engine operates on
    # simulation timescale — no wall-clock sleep needed.
    tmpdir = tempfile.mkdtemp(prefix="ns3_incast_")
    agg_nc = ns.NodeContainer()
    agg_nc.Add(agg)
    access.EnablePcap(os.path.join(tmpdir, "burst"), agg_nc)

    print(
        f"Running ns3 simulation: {n} senders × {config.number_of_waves} waves "
        f"({sim_end_s:.1f}s sim time) …"
    )
    ns.Simulator.Stop(ns.Seconds(sim_end_s + 1.0))
    ns.Simulator.Run()
    ns.Simulator.Destroy()

    # Parse per-sender PCAP files and merge by simulation timestamp.
    # access.EnablePcap also captures the aggregator's bottleneck-facing device
    # (device index n+1, post-DropTailQueue) — exclude it to avoid duplicates.
    # Access link devices are 1..n on aggregator node n; bottleneck is n+1.
    sender_ips_set = set(sender_ips.values())
    agg_node_id = n
    bottleneck_pcap = os.path.join(tmpdir, f"burst-{agg_node_id}-{n + 1}.pcap")
    pre_queue: list[tuple[int, int, str]] = []
    for pcap in globmod.glob(os.path.join(tmpdir, "burst-*.pcap")):
        if os.path.abspath(pcap) == os.path.abspath(bottleneck_pcap):
            continue  # post-bottleneck — skip to avoid counting packets twice
        for ts_us, ip_total, src_ip in _iter_pcap_tcp_data(pcap):
            if src_ip in sender_ips_set:
                pre_queue.append((ts_us, ip_total, src_ip))

    pre_queue.sort()
    print(f"Simulation done: {len(pre_queue)} TCP data packets captured (pre-bottleneck).")
    return pre_queue, control_ips


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

        for ts_us, ip_total, src_ip in packets:
            if src_ip in control_ips:
                tc, tag = "control", config.control_priority_tag
            else:
                tc, tag = "bulk", config.bulk_priority_tag

            # ts_us is the ns3 simulation timestamp; the engine uses it as
            # arrival_time_us so simulation operates on ns3 timescale, not
            # wall-clock time.  No sleep needed — correctness is driven by ts_us.
            conn.sendall(f"{ip_total},{tc},{tag},{ts_us}\n".encode())

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
