#ifndef CN_HEADER_H
#define CN_HEADER_H

#include <stdint.h>
#include "ns3/header.h"
#include "ns3/buffer.h"

namespace ns3 {

/**
 * \ingroup Cn
 * \brief Header for the Congestion Notification Message
 *
 * This class has two fields: The five-tuple flow id and the quantized
 * congestion level. This can be serialized to or deserialzed from a byte
 * buffer.
 */

class CnHeader : public Header 
{
public:
  CnHeader (const uint16_t fid, uint8_t qIndex, uint8_t ecnbits, uint16_t qfb, uint16_t total);
  //CnHeader (const uint16_t fid, uint8_t qIndex, uint8_t qfb);
  CnHeader ();
  virtual ~CnHeader ();

//Setters
  /**
   * \param fid The flow id
   */
  void SetFlow (const uint16_t fid);
  /**
   * \param q The quantized feedback value
   */
  void SetQfb (uint16_t q);
  void SetTotal (uint16_t t);

//Getters
  /**
   * \return The flow id
   */
  uint16_t GetFlow () const;
  /**
   * \return The quantized feedback value
   */
  uint16_t GetQfb () const;
  uint16_t GetTotal () const;


  void SetQindex (const uint8_t qIndex);
  uint8_t GetQindex () const;

  void SetECNBits (const uint8_t ecnbits);
  uint8_t GetECNBits () const;

  void SetSeq (const uint32_t seq);
  uint32_t GetSeq () const;

  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);

private:
  uint16_t sport, dport;
  uint16_t m_fid;
  uint8_t m_qIndex;
  uint8_t m_ecnBits;
  union {
	  struct {
		  uint16_t m_qfb;
		  uint16_t m_total;
	  };
	  uint32_t m_seq;
  };
};

}; // namespace ns3

#endif /* CN_HEADER */
