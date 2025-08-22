#ifndef RDMA_CLIENT_H
#define RDMA_CLIENT_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-address.h"
#include <ns3/rdma.h>

namespace ns3 {

class Socket;
class Packet;

/**
 * \ingroup rdmaclientserver
 * \class rdmaClient
 * \brief A RDMA client.
 *
 */
class RdmaClient : public Application
{
public:
  static TypeId
  GetTypeId (void);

  RdmaClient ();

  virtual ~RdmaClient ();

  /**
   * \brief set the remote address and port
   * \param ip remote IP address
   * \param port remote port
   */
  void SetRemote (Ipv4Address ip, uint16_t port);
  void SetLocal (Ipv4Address ip, uint16_t port);
  void SetPG (uint16_t pg);
  void SetSize(uint64_t size);
  void Finish();

protected:
  virtual void DoDispose (void);

private:

  virtual void StartApplication (void);
  virtual void StopApplication (void);

  uint64_t m_size;
  uint16_t m_pg;

  Ipv4Address m_sip, m_dip;
  uint16_t m_sport, m_dport;
  uint32_t m_win; // bound of on-the-fly packets
  uint64_t m_baseRtt; // base Rtt

  Time stopTime;
};

} // namespace ns3

#endif /* RDMA_CLIENT_H */