/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Singleton.h>
#include <Kernel/Devices/RandomDevice.h>
#include <Kernel/Net/NetworkAdapter.h>
#include <Kernel/Net/Routing.h>
#include <Kernel/Net/UDP.h>
#include <Kernel/Net/UDPSocket.h>
#include <Kernel/Process.h>
#include <Kernel/Random.h>

namespace Kernel {

void UDPSocket::for_each(Function<void(const UDPSocket&)> callback)
{
    sockets_by_port().for_each_shared([&](const auto& socket) {
        callback(*socket.value);
    });
}

static Singleton<ProtectedValue<HashMap<u16, UDPSocket*>>> s_map;

ProtectedValue<HashMap<u16, UDPSocket*>>& UDPSocket::sockets_by_port()
{
    return *s_map;
}

SocketHandle<UDPSocket> UDPSocket::from_port(u16 port)
{
    return sockets_by_port().with_shared([&](const auto& table) -> SocketHandle<UDPSocket> {
        RefPtr<UDPSocket> socket;
        auto it = table.find(port);
        if (it == table.end())
            return {};
        socket = (*it).value;
        VERIFY(socket);
        return { *socket };
    });
}

UDPSocket::UDPSocket(int protocol, NonnullOwnPtr<DoubleBuffer> receive_buffer)
    : IPv4Socket(SOCK_DGRAM, protocol, move(receive_buffer), {})
{
}

UDPSocket::~UDPSocket()
{
    sockets_by_port().with_exclusive([&](auto& table) {
        table.remove(local_port());
    });
}

KResultOr<NonnullRefPtr<UDPSocket>> UDPSocket::create(int protocol, NonnullOwnPtr<DoubleBuffer> receive_buffer)
{
    auto socket = adopt_ref_if_nonnull(new (nothrow) UDPSocket(protocol, move(receive_buffer)));
    if (socket)
        return socket.release_nonnull();
    return ENOMEM;
}

KResultOr<size_t> UDPSocket::protocol_receive(ReadonlyBytes raw_ipv4_packet, UserOrKernelBuffer& buffer, size_t buffer_size, [[maybe_unused]] int flags)
{
    auto& ipv4_packet = *(const IPv4Packet*)(raw_ipv4_packet.data());
    auto& udp_packet = *static_cast<const UDPPacket*>(ipv4_packet.payload());
    VERIFY(udp_packet.length() >= sizeof(UDPPacket)); // FIXME: This should be rejected earlier.
    size_t read_size = min(buffer_size, udp_packet.length() - sizeof(UDPPacket));
    if (!buffer.write(udp_packet.payload(), read_size))
        return EFAULT;
    return read_size;
}

KResultOr<size_t> UDPSocket::protocol_send(const UserOrKernelBuffer& data, size_t data_length)
{
    auto routing_decision = route_to(peer_address(), local_address(), bound_interface());
    if (routing_decision.is_zero())
        return EHOSTUNREACH;
    auto ipv4_payload_offset = routing_decision.adapter->ipv4_payload_offset();
    data_length = min(data_length, routing_decision.adapter->mtu() - ipv4_payload_offset - sizeof(UDPPacket));
    const size_t udp_buffer_size = sizeof(UDPPacket) + data_length;
    auto packet = routing_decision.adapter->acquire_packet_buffer(ipv4_payload_offset + udp_buffer_size);
    if (!packet)
        return ENOMEM;
    memset(packet->buffer->data() + ipv4_payload_offset, 0, sizeof(UDPPacket));
    auto& udp_packet = *reinterpret_cast<UDPPacket*>(packet->buffer->data() + ipv4_payload_offset);
    udp_packet.set_source_port(local_port());
    udp_packet.set_destination_port(peer_port());
    udp_packet.set_length(udp_buffer_size);
    if (!data.read(udp_packet.payload(), data_length))
        return EFAULT;

    routing_decision.adapter->fill_in_ipv4_header(*packet, local_address(), routing_decision.next_hop,
        peer_address(), IPv4Protocol::UDP, udp_buffer_size, ttl());
    routing_decision.adapter->send_packet(packet->bytes());
    return data_length;
}

KResult UDPSocket::protocol_connect(FileDescription&, ShouldBlock)
{
    m_role = Role::Connected;
    set_connected(true);
    return KSuccess;
}

KResultOr<u16> UDPSocket::protocol_allocate_local_port()
{
    constexpr u16 first_ephemeral_port = 32768;
    constexpr u16 last_ephemeral_port = 60999;
    constexpr u16 ephemeral_port_range_size = last_ephemeral_port - first_ephemeral_port;
    u16 first_scan_port = first_ephemeral_port + get_good_random<u16>() % ephemeral_port_range_size;

    return sockets_by_port().with_exclusive([&](auto& table) -> KResultOr<u16> {
        for (u16 port = first_scan_port;;) {
            auto it = table.find(port);
            if (it == table.end()) {
                set_local_port(port);
                table.set(port, this);
                return port;
            }
            ++port;
            if (port > last_ephemeral_port)
                port = first_ephemeral_port;
            if (port == first_scan_port)
                break;
        }
        return EADDRINUSE;
    });
}

KResult UDPSocket::protocol_bind()
{
    return sockets_by_port().with_exclusive([&](auto& table) -> KResult {
        if (table.contains(local_port()))
            return EADDRINUSE;
        table.set(local_port(), this);
        return KSuccess;
    });
}

}
