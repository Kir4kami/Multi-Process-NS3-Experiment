#include "rdma-client-helper.h"
#include "ns3/rdma-client.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"

namespace ns3 {

RdmaClientHelper::RdmaClientHelper ()
{
}

RdmaClientHelper::RdmaClientHelper (uint16_t pg, Ipv4Address sip, Ipv4Address dip, uint16_t sport, uint16_t dport, uint64_t size, uint32_t win, uint64_t baseRtt,Time stopTime)
{
	m_factory.SetTypeId (RdmaClient::GetTypeId ());
	SetAttribute ("PriorityGroup", UintegerValue (pg));
	SetAttribute ("SourceIP", Ipv4AddressValue (sip));
	SetAttribute ("DestIP", Ipv4AddressValue (dip));
	SetAttribute ("SourcePort", UintegerValue (sport));
	SetAttribute ("DestPort", UintegerValue (dport));
	SetAttribute ("WriteSize", UintegerValue (size));
	SetAttribute ("Window", UintegerValue (win));
	SetAttribute ("BaseRtt", UintegerValue (baseRtt));
	SetAttribute ("stopTime", TimeValue(stopTime));
}

void
RdmaClientHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
RdmaClientHelper::Install (NodeContainer c)
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;
      Ptr<RdmaClient> client = m_factory.Create<RdmaClient> ();
      node->AddApplication (client);
      apps.Add (client);
    }
  return apps;
}

} // namespace ns3