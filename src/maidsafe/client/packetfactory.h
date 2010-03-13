/*
* ============================================================================
*
* Copyright [2009] maidsafe.net limited
*
* Description:  Factory for system signature packets
* Version:      1.0
* Created:      2009-01-29-00.23.23
* Revision:     none
* Compiler:     gcc
* Author:       Fraser Hutchison (fh), fraser.hutchison@maidsafe.net
* Company:      maidsafe.net limited
*
* The following source code is property of maidsafe.net limited and is not
* meant for external use.  The use of this code is governed by the license
* file LICENSE.TXT found in the root of this directory and also on
* www.maidsafe.net.
*
* You are not free to copy, amend or otherwise use this source code without
* the explicit written permission of the board of directors of maidsafe.net.
*
* ============================================================================
*/

#ifndef MAIDSAFE_CLIENT_PACKETFACTORY_H_
#define MAIDSAFE_CLIENT_PACKETFACTORY_H_

#include <boost/any.hpp>
#include <boost/thread.hpp>
#include <maidsafe/crypto.h>
#include <map>
#include <queue>
#include <string>

#include "protobuf/datamaps.pb.h"

namespace maidsafe {

const boost::uint16_t kRsaKeySize = 4096;  // size to generate RSA keys in bits.
const boost::uint16_t kNoOfSystemPackets = 8;
const boost::uint16_t kMaxCryptoThreadCount = 3;

typedef std::map<std::string, boost::any> PacketParams;

class CryptoKeyPairs {
 public:
  CryptoKeyPairs();
  ~CryptoKeyPairs();
  void Init(const boost::uint16_t &max_thread_count,
            const boost::uint16_t &buffer_count);
  crypto::RsaKeyPair GetKeyPair();
  boost::uint16_t max_thread_count();
  boost::uint16_t buffer_count();
  void set_max_thread_count(const boost::uint16_t &max_thread_count);
  void set_buffer_count(const boost::uint16_t &buffer_count);
 private:
  CryptoKeyPairs &operator=(const CryptoKeyPairs&);
  CryptoKeyPairs(const CryptoKeyPairs&);
  void CreateThread();
  void DestroyThread();
  void CreateKeyPair();
  boost::uint16_t max_thread_count_, buffer_count_, running_thread_count_;
  std::queue<crypto::RsaKeyPair> key_buffer_;
  boost::mutex kb_mutex_;
  boost::condition_variable kb_cond_var_;
  std::map< boost::thread::id, boost::shared_ptr<boost::thread> > threads_;
};

class Packet {
 public:
  explicit Packet(const crypto::RsaKeyPair &rsakp);
  virtual ~Packet() {}
  virtual PacketParams Create(PacketParams *params) = 0;
  virtual PacketParams GetData(const std::string &serialised_packet);
  bool ValidateSignature(const std::string &serialised_packet,
                         const std::string &public_key);
 protected:
  crypto::Crypto crypto_obj_;
  crypto::RsaKeyPair rsakp_;
 private:
  Packet &operator=(const Packet&);
  Packet(const Packet&);
};

class PacketFactory {
 public:
  static boost::shared_ptr<Packet> Factory(PacketType type,
                                           const crypto::RsaKeyPair &rsakp);
 private:
  PacketFactory &operator=(const PacketFactory&);
  PacketFactory(const PacketFactory&);
};

}  // namespace maidsafe

#endif  // MAIDSAFE_CLIENT_PACKETFACTORY_H_
