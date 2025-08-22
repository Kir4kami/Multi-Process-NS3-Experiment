#include <stdint.h>
#include <iostream>
#include "cn-header.h"
#include "ns3/buffer.h"
#include "ns3/address-utils.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE ("CnHeader");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (CnHeader);

CnHeader::CnHeader (const uint16_t fid, uint8_t qIndex, uint8_t ecnbits, uint16_t qfb, uint16_t total)
  : m_fid(fid), m_qIndex(qIndex), m_qfb(qfb), m_ecnBits(ecnbits), m_total(total)
{
  //NS_LOG_LOGIC("CN got the flow id " << std::hex << m_fid.hi << "+" << m_fid.lo << std::dec);
}

CnHeader::CnHeader ()
  : m_fid(), m_qIndex(), m_qfb(0), m_ecnBits(0)
{}

CnHeader::~CnHeader ()
{}

void CnHeader::SetFlow (const uint16_t fid)
{
  m_fid = fid;
}

void CnHeader::SetQindex (const uint8_t qIndex)
{
	m_qIndex = qIndex;
}

void CnHeader::SetQfb (uint16_t q)
{
  m_qfb = q;
}

void CnHeader::SetTotal (uint16_t total)
{
	m_total = total;
}

void CnHeader::SetECNBits (const uint8_t ecnbits)
{
	m_ecnBits = ecnbits;
}


uint16_t CnHeader::GetFlow () const
{
  return m_fid;
}

uint8_t CnHeader::GetQindex () const
{
	return m_qIndex;
}

uint16_t CnHeader::GetQfb () const
{
  return m_qfb;
}

uint16_t CnHeader::GetTotal() const
{
	return m_total;
}

uint8_t CnHeader::GetECNBits() const
{
	return m_ecnBits;
}

void CnHeader::SetSeq (const uint32_t seq){
	m_seq = seq;
}

uint32_t CnHeader::GetSeq () const{
	return m_seq;
}
TypeId 
CnHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::CnHeader")
    .SetParent<Header> ()
    .AddConstructor<CnHeader> ()
    ;
  return tid;
}
TypeId 
CnHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
void CnHeader::Print (std::ostream &os) const
{
  //m_fid.Print(os);
  os << " qFb=" << (unsigned) m_qfb << "/" << (unsigned) m_total;
}
uint32_t CnHeader::GetSerializedSize (void)  const
{
  return 8;
}
void CnHeader::Serialize (Buffer::Iterator start)  const
{
  start.WriteU8(m_qIndex);
  start.WriteU16(m_fid);
  start.WriteU8(m_ecnBits);
  start.WriteU16(m_qfb);
  start.WriteU16(m_total);
}

uint32_t CnHeader::Deserialize (Buffer::Iterator start)
{
  m_qIndex = start.ReadU8();
  m_fid = start.ReadU16();
  m_ecnBits = start.ReadU8();
  m_qfb = start.ReadU16();
  m_total = start.ReadU16();
  return GetSerializedSize ();
}


}; // namespace ns3
