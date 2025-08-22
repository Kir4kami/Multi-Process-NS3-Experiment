#ifndef PAUSE_HEADER_H
#define PAUSE_HEADER_H

#include <stdint.h>
#include "ns3/header.h"
#include "ns3/buffer.h"

namespace ns3 {

/**
 * \ingroup Pause
 * \brief Header for the Congestion Notification Message
 *
 * This class has two fields: The five-tuple flow id and the quantized
 * congestion level. This can be serialized to or deserialzed from a byte
 * buffer.
 */

class PauseHeader : public Header
{
public:
  PauseHeader (uint32_t time,uint32_t qlen,uint8_t qindex);
  PauseHeader ();
  virtual ~PauseHeader ();

//Setters
  /**
   * \param time The pause time in microseconds
   */
  void SetTime (uint32_t time);
  void SetQLen (uint32_t qlen);
  void SetQIndex (uint8_t qindex);

//Getters
  /**
   * \return The pause time in microseconds
   */
  uint32_t GetTime () const;
  uint32_t GetQLen () const;
  uint8_t GetQIndex () const;

  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);

private:
  uint32_t m_time;
  uint32_t m_qlen;
  uint8_t m_qindex;
};

}; // namespace ns3

#endif /* PAUSE_HEADER */
