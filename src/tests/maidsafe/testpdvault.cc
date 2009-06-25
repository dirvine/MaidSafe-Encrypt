/*
* ============================================================================
*
* Copyright [2009] maidsafe.net limited
*
* Description:  Test for pdvault
* Version:      1.0
* Created:      2009-03-23-21.28.20
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

#include <boost/thread/thread.hpp>
#include <boost/thread/xtime.hpp>
#include <gtest/gtest.h>
#include <map>
#include <vector>

#include "maidsafe/crypto.h"
#include "maidsafe/maidsafe-dht.h"
#include "maidsafe/utils.h"
#include "maidsafe/client/pdclient.h"
#include "maidsafe/client/systempackets.h"
#include "maidsafe/vault/pdvault.h"
#include "tests/maidsafe/localvaults.h"

static bool callback_timed_out_ = true;
static bool callback_succeeded_ = false;
static std::string callback_content_ = "";
static bool callback_prepared_ = false;
static std::list<std::string> callback_msgs;

namespace testpdvault {

inline void DeleteCallback(const std::string &result) {
  maidsafe::DeleteResponse resp;
  if (!resp.ParseFromString(result) ||
      resp.result() != kCallbackSuccess) {
    callback_succeeded_ = false;
    callback_timed_out_ = false;
  } else {
    callback_succeeded_ = true;
    callback_timed_out_ = false;
  }
}

inline void GetMessagesCallback(const std::string &result) {
  maidsafe::GetMessagesResponse resp;
  if (!resp.ParseFromString(result) ||
      resp.result() != kCallbackSuccess) {
    callback_succeeded_ = false;
    callback_timed_out_ = false;
  } else {
    callback_succeeded_ = true;
    callback_timed_out_ = false;
    for (int i = 0; i < resp.messages_size(); i++) {
      callback_msgs.push_back(resp.messages(i));
    }
  }
}

void PrepareCallbackResults() {
  callback_timed_out_ = true;
  callback_succeeded_ = false;
  callback_content_ = "";
  callback_prepared_ = true;
  callback_msgs.clear();
}

static void GeneralCallback(const std::string &result) {
  base::GeneralResponse result_msg;
  if ((!result_msg.ParseFromString(result))||
      (result_msg.result() != kad::kRpcResultSuccess)) {
    callback_succeeded_ = false;
    callback_timed_out_ = false;
  } else {
    callback_succeeded_ = true;
    callback_timed_out_ = false;
  }
}

static void StoreChunkCallback(const std::string &result) {
  maidsafe::StoreResponse result_msg;
  if ((!result_msg.ParseFromString(result))||
      (result_msg.result() != kCallbackSuccess)) {
    callback_succeeded_ = false;
    callback_timed_out_ = false;
  } else {
    callback_succeeded_ = true;
    callback_timed_out_ = false;
  }
}

static void GetChunkCallback(const std::string &result) {
  maidsafe::GetResponse result_msg;
  if ((!result_msg.ParseFromString(result))||
      (result_msg.result() != kCallbackSuccess)) {
    callback_succeeded_ = false;
    callback_timed_out_ = false;
  } else {
    callback_succeeded_ = true;
    callback_timed_out_ = false;
    callback_content_ = result_msg.content();
  }
}

void BluddyWaitFunction(int seconds, boost::mutex* mutex) {
  if (!callback_prepared_) {
    printf("Callback result variables were not set.\n");
    return;
  }
  bool got_callback = false;
  // for (int i = 0; i < seconds*100; ++i) {
  while (!got_callback) {
    {
      boost::mutex::scoped_lock lock_(*mutex);
      if (!callback_timed_out_) {
        got_callback = true;
        if (callback_succeeded_) {
  //        printf("Callback succeeded after %3.2f seconds\n",
  //               static_cast<float>(i)/100);
          callback_prepared_ = false;
          return;
        } else {
  //        printf("Callback failed after %3.2f seconds\n",
  //               static_cast<float>(i)/100);
          callback_prepared_ = false;
          return;
        }
      }
    }
    boost::this_thread::sleep(boost::posix_time::milliseconds(10));
  }
  callback_prepared_ = false;
  printf("Callback timed out after %i second(s)\n", seconds);
}

void MakeChunks(const fs::path &test_chunkstore,
                int no_of_chunks,
                std::map<std::string, std::string> *chunks) {
  maidsafe_crypto::Crypto cryobj_;
  cryobj_.set_hash_algorithm("SHA512");
  cryobj_.set_symm_algorithm("AES_256");
  for (int i = 0; i < no_of_chunks; ++i) {
    std::string chunk_content_ = base::RandomString(100);
    std::string chunk_name_ = cryobj_.Hash(chunk_content_,
                                           "",
                                           maidsafe_crypto::STRING_STRING,
                                           false);
    fs::path chunk_path_(test_chunkstore);
    std::string hex_chunk_name_("");
    base::encode_to_hex(chunk_name_, hex_chunk_name_);
    chunk_path_ /= hex_chunk_name_;
    std::ofstream ofs_;
    ofs_.open(chunk_path_.string().c_str());
    ofs_ << chunk_content_;
    ofs_.close();
    chunks->insert(std::pair<std::string, std::string>
        (chunk_name_, chunk_content_));
  }
}

void CreateSystemPacket(const std::string &priv_key,
                        std::string *packet_name,
                        std::string *ser_packet) {
  maidsafe_crypto::Crypto co;
  co.set_hash_algorithm("SHA512");
  packethandler::GenericPacket gp;
  gp.set_data(base::RandomString(4096));
  gp.set_signature(co.AsymSign(gp.data(), "", priv_key,
    maidsafe_crypto::STRING_STRING));
  gp.SerializeToString(ser_packet);
  *packet_name = co.Hash(*ser_packet, "", maidsafe_crypto::STRING_STRING,
                         false);
}

void CreateBufferPacket(const std::string &owner,
                        const std::string  &public_key,
                        const std::string  &private_key,
                        std::string *packet_name,
                        std::string *ser_packet) {
  maidsafe_crypto::Crypto co;
  co.set_hash_algorithm("SHA512");
  *packet_name = co.Hash(owner + "BUFFER", "", maidsafe_crypto::STRING_STRING,
                         false);
  packethandler::BufferPacket buffer_packet;
  packethandler::GenericPacket *ser_owner_info= buffer_packet.add_owner_info();
  packethandler::BufferPacketInfo buffer_packet_info;
  buffer_packet_info.set_owner(owner);
  buffer_packet_info.set_ownerpublickey(public_key);
  buffer_packet_info.set_online(false);
  std::string ser_info;
  buffer_packet_info.SerializeToString(&ser_info);
  ser_owner_info->set_data(ser_info);
  ser_owner_info->set_signature(co.AsymSign(ser_info, "", private_key,
    maidsafe_crypto::STRING_STRING));
  buffer_packet.SerializeToString(ser_packet);
}

void CreateMessage(const std::string &message,
                   const std::string &public_key,
                   const std::string &private_key,
                   const std::string &sender_id,
                   const packethandler::MessageType &m_type,
                   std::string *ser_message,
                   std::string *ser_expected_msg) {
  std::string key("AESkey");
  maidsafe_crypto::Crypto co;
  co.set_hash_algorithm("SHA512");
  co.set_symm_algorithm("AES_256");
  packethandler::BufferPacketMessage bpmsg;
  bpmsg.set_sender_id(sender_id);
  bpmsg.set_rsaenc_key(co.AsymEncrypt(key, "", public_key,
    maidsafe_crypto::STRING_STRING));
  bpmsg.set_aesenc_message(co.SymmEncrypt(message, "",
    maidsafe_crypto::STRING_STRING, key));
  bpmsg.set_type(m_type);
  bpmsg.set_sender_public_key(public_key);
  std::string ser_bpmsg;
  bpmsg.SerializeToString(&ser_bpmsg);
  packethandler::GenericPacket bpmsg_gp;
  bpmsg_gp.set_data(ser_bpmsg);
  bpmsg_gp.set_signature(co.AsymSign(ser_bpmsg, "", private_key,
    maidsafe_crypto::STRING_STRING));
  std::string ser_bpmsg_gp;
  bpmsg_gp.SerializeToString(ser_message);

  // Expected result for GetMsgs
  packethandler::ValidatedBufferPacketMessage val_msg;
  val_msg.set_index(bpmsg.rsaenc_key());
  val_msg.set_message(bpmsg.aesenc_message());
  val_msg.set_sender(bpmsg.sender_id());
  val_msg.set_type(bpmsg.type());
  val_msg.SerializeToString(ser_expected_msg);
}

}  // namespace testpdvault

namespace maidsafe_vault {

static std::vector< boost::shared_ptr<PDVault> > pdvaults_;
static const int kNetworkSize_ = 10;
static const int kTestK_ = 4;

class TestPDVault : public testing::Test {
 protected:
  TestPDVault() : kad_config_file_(".kadconfig"),
                  client_chunkstore_dir_("TestVault/ClientChunkstore"),
                  client_datastore_dir_("TestVault/ClientDatastore"),
                  chunkstore_dirs_(),
                  pdclient_(),
                  client_keys_(),
                  client_public_key_(""),
                  client_private_key_(""),
                  client_signed_public_key_(""),
                  mutex_(),
                  crypto_() {
//    fs::path temp_("PDVaultTest");
//    try {
//      if (fs::exists(temp_))
//        fs::remove_all(temp_);
//    }
//    catch(const std::exception &e) {
//      printf("%s\n", e.what());
//    }
    fs::create_directories(client_chunkstore_dir_);
    fs::create_directories(client_datastore_dir_);
    crypto_.set_hash_algorithm("SHA512");
    crypto_.set_symm_algorithm("AES_256");
    client_keys_.GenerateKeys(packethandler::kRsaKeySize);
    client_public_key_ = client_keys_.public_key();
    client_private_key_ = client_keys_.private_key();
    client_signed_public_key_ = crypto_.AsymSign(
                                    client_keys_.public_key(),
                                    "",
                                    client_keys_.private_key(),
                                    maidsafe_crypto::STRING_STRING);
  }

  virtual ~TestPDVault() {}

  virtual void SetUp() {
    boost::shared_ptr<maidsafe::PDClient>
        pdclient_local_(new maidsafe::PDClient(client_datastore_dir_, 63001,
                                               kad_config_file_));
    pdclient_ = pdclient_local_;
    testpdvault::PrepareCallbackResults();
    pdclient_->Join("", boost::bind(&testpdvault::GeneralCallback, _1));
    testpdvault::BluddyWaitFunction(60, &mutex_);
    ASSERT_TRUE(callback_succeeded_);
    ASSERT_FALSE(callback_timed_out_);
  }

  virtual void TearDown() {
    testpdvault::PrepareCallbackResults();
    pdclient_->Leave(boost::bind(&testpdvault::GeneralCallback, _1));
    testpdvault::BluddyWaitFunction(60, &mutex_);
    ASSERT_TRUE(callback_succeeded_);
    ASSERT_FALSE(callback_timed_out_);
    pdclient_.reset();
    printf("#### CLIENT STOPPPED\n");
  }

  std::string kad_config_file_, client_chunkstore_dir_, client_datastore_dir_;
  std::vector<fs::path> chunkstore_dirs_;
  boost::shared_ptr<maidsafe::PDClient> pdclient_;
  maidsafe_crypto::RsaKeyPair client_keys_;
  std::string client_public_key_, client_private_key_;
  std::string client_signed_public_key_;
  boost::mutex mutex_;
  maidsafe_crypto::Crypto crypto_;

 private:
  TestPDVault(const TestPDVault&);
  TestPDVault &operator=(const TestPDVault&);
};

TEST_F(TestPDVault, FUNC_MAID_VaultStartStop) {
  // check pdvaults can be started and stopped multiple times
  bool success_(false);
  const int kTestVaultNo(4);
  for (int loop = 0; loop < 2; ++loop) {
    success_ = false;
    pdvaults_[kTestVaultNo]->Stop();
    ASSERT_FALSE(pdvaults_[kTestVaultNo]->vault_started());
    printf("Vault stopped - iteration %i.\n\n", loop+1);
//    // checking kadconfig file
//    std::string kadconfig_path(datastore_dir_+"/Datastore"+
//        base::itos(64001+kTestVaultNo) + "/.kadconfig");
//    base::KadConfig kconf;
//    ASSERT_TRUE(boost::filesystem::exists(
//        boost::filesystem::path(kadconfig_path)));
//    std::ifstream kadconf_file(kadconfig_path.c_str(),
//        std::ios::in | std::ios::binary);
//    ASSERT_TRUE(kconf.ParseFromIstream(&kadconf_file));
//    kadconf_file.close();
//    ASSERT_LT(0, kconf.contact_size());
    pdvaults_[kTestVaultNo]->Start(false);
    ASSERT_TRUE(pdvaults_[kTestVaultNo]->vault_started());
    printf("Vault started - iteration %i.\n\n", loop+1);
  }
}

TEST_F(TestPDVault, FUNC_MAID_StoreChunks) {
  // add some valid chunks to client chunkstore and store to network
  std::map<std::string, std::string> chunks_;
  const boost::uint32_t kNumOfTestChunks(5);
  testpdvault::MakeChunks(client_chunkstore_dir_, kNumOfTestChunks, &chunks_);
  std::map<std::string, std::string>::iterator it_;
  int i = 0;
  for (it_ = chunks_.begin(); it_ != chunks_.end(); ++it_) {
//    printf("Saving chunk: %s\n", (*it_).first.c_str());
    std::string chunk_name = (*it_).first;
    std::string hex_chunk_name;
    base::encode_to_hex(chunk_name, hex_chunk_name);
    std::string signed_request_ =
        crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                      client_signed_public_key_+hex_chunk_name,
                                      "",
                                      maidsafe_crypto::STRING_STRING,
                                      true),
                         "",
                         client_private_key_,
                         maidsafe_crypto::STRING_STRING);
    testpdvault::PrepareCallbackResults();
    printf("\tIn TestPDVault, before store chunk %s.\n",
           hex_chunk_name.c_str());
    pdclient_->StoreChunk((*it_).first,
                          (*it_).second,
                          client_public_key_,
                          client_signed_public_key_,
                          signed_request_,
                          maidsafe::DATA,
                          boost::bind(&testpdvault::StoreChunkCallback,
                                      _1));
    testpdvault::BluddyWaitFunction(120, &mutex_);
    printf("\tIn TestPDVault, after store chunk %d.\n", i);
    ASSERT_TRUE(callback_succeeded_);
    ASSERT_FALSE(callback_timed_out_);
    boost::this_thread::sleep(boost::posix_time::seconds(8));
    i++;
  }
//  // iterate through all vault chunkstores to ensure each chunk stored
//  // enough times and each chunk copy is valid (i.e. name == Hash(contents))
//  for (it_ = chunks_.begin(); it_ != chunks_.end(); ++it_) {
//    std::string hash_;
//    base::encode_to_hex((*it_).first, hash_);
//    int chunk_count_ = 0;
//    for (int vault_no_ = 0; vault_no_ < kNetworkSize_; ++vault_no_) {
//      fs::directory_iterator end_itr_;
//      for (fs::directory_iterator itr_(chunkstore_dirs_[vault_no_]);
//           itr_ != end_itr_;
//           ++itr_) {
//        if (!is_directory(itr_->status())) {
//          if (itr_->filename() == hash_) {
//            // printf("Chunk: %s\n", itr_->path().string().c_str());
//            ++chunk_count_;
//          }
//        }
//      }
//    }
//    ASSERT_EQ(kTestK_, chunk_count_);
//  }
  boost::this_thread::sleep(boost::posix_time::seconds(5));
}

TEST_F(TestPDVault, FUNC_MAID_GetChunk) {
  std::map<std::string, std::string> chunks_;
  const boost::uint32_t kNumOfTestChunks(3);
  testpdvault::MakeChunks(client_chunkstore_dir_, kNumOfTestChunks, &chunks_);
  std::map<std::string, std::string>::iterator it_;
  int i = 0;
  for (it_ = chunks_.begin(); it_ != chunks_.end(); ++it_) {
//    printf("Saving chunk: %s\n", (*it_).first.c_str());
    std::string chunk_name = (*it_).first;
    std::string chunk_name_enc;
    base::encode_to_hex(chunk_name, chunk_name_enc);
    std::string signed_request_ =
        crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                      client_signed_public_key_+chunk_name_enc,
                                      "",
                                      maidsafe_crypto::STRING_STRING,
                                      true),
                         "",
                         client_private_key_,
                         maidsafe_crypto::STRING_STRING);
    testpdvault::PrepareCallbackResults();
    i++;
    printf("before store chunk %d\n", i);
    pdclient_->StoreChunk(chunk_name,
                          (*it_).second,
                          client_public_key_,
                          client_signed_public_key_,
                          signed_request_,
                          maidsafe::DATA,
                          boost::bind(&testpdvault::StoreChunkCallback,
                                      _1));
    testpdvault::BluddyWaitFunction(120, &mutex_);
    printf("after store chunk %d\n", i);
    ASSERT_TRUE(callback_succeeded_);
    ASSERT_FALSE(callback_timed_out_);
    boost::this_thread::sleep(boost::posix_time::seconds(8));
  }
  // Check each chunk can be retrieved correctly
  for (it_ = chunks_.begin(); it_ != chunks_.end(); ++it_) {
    printf("getting chunk\n");
    testpdvault::PrepareCallbackResults();
    pdclient_->GetChunk((*it_).first,
                        boost::bind(&testpdvault::GetChunkCallback, _1));
    testpdvault::BluddyWaitFunction(60, &mutex_);
    ASSERT_TRUE(callback_succeeded_);
    ASSERT_EQ(callback_content_, (*it_).second);
    ASSERT_FALSE(callback_timed_out_);
    std::string hash = crypto_.Hash(callback_content_,
                                     "",
                                     maidsafe_crypto::STRING_STRING,
                                     false);
    ASSERT_EQ((*it_).first, hash);
    boost::this_thread::sleep(boost::posix_time::seconds(8));
  }
}

TEST_F(TestPDVault, FUNC_MAID_StoreChunkInvalidRequest) {
  std::map<std::string, std::string> chunks;
  const boost::uint32_t kNumOfTestChunks(1);
  testpdvault::MakeChunks(client_chunkstore_dir_, kNumOfTestChunks, &chunks);
  std::map<std::string, std::string>::iterator it;
  std::string chunk_name;
  for (it = chunks.begin(); it != chunks.end(); ++it)
    chunk_name = (*it).first;

  std::string enc_chunk_name;
  base::encode_to_hex(chunk_name, enc_chunk_name);

  // creating a the signature
  std::string signed_request =
      crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                    client_signed_public_key_+enc_chunk_name,
                                    "",
                                    maidsafe_crypto::STRING_STRING,
                                    true),
                       "",
                       client_private_key_,
                        maidsafe_crypto::STRING_STRING);

  // creating another pair of keys
  maidsafe_crypto::RsaKeyPair keys;
  keys.GenerateKeys(packethandler::kRsaKeySize);
  ASSERT_NE(client_public_key_, keys.public_key());
  std::string sig_pub_key = crypto_.AsymSign(keys.public_key(),
                                             "",
                                             keys.private_key(),
                                             maidsafe_crypto::STRING_STRING);

  testpdvault::PrepareCallbackResults();
  pdclient_->StoreChunk(chunk_name,
                        chunks[chunk_name],
                        keys.public_key(),
                        sig_pub_key,
                        signed_request,
                        maidsafe::DATA,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                      _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  testpdvault::PrepareCallbackResults();
  // sending invalid public key
  pdclient_->StoreChunk(chunk_name,
                        chunks[chunk_name],
                        std::string("invalid key"),
                        sig_pub_key,
                        signed_request,
                        maidsafe::DATA,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                      _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  testpdvault::PrepareCallbackResults();

  // sending invalid request
  signed_request =
      crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                    client_signed_public_key_+chunk_name,
                                    "",
                                    maidsafe_crypto::STRING_STRING,
                                    true),
                       "",
                       client_private_key_,
                       maidsafe_crypto::STRING_STRING);

  pdclient_->StoreChunk(chunk_name,
                        chunks[chunk_name],
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request,
                        maidsafe::DATA,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                      _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
}

TEST_F(TestPDVault, FUNC_MAID_StoreSystemPacket) {
  std::string chunk_name, chunk_content;
  testpdvault::CreateSystemPacket(client_private_key_, &chunk_name,
    &chunk_content);
  std::string hex_chunk_name;
  base::encode_to_hex(chunk_name, hex_chunk_name);
  std::string signed_request =
      crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                    client_signed_public_key_+hex_chunk_name,
                                    "",
                                    maidsafe_crypto::STRING_STRING,
                                    true),
                       "",
                       client_private_key_,
                       maidsafe_crypto::STRING_STRING);
  testpdvault::PrepareCallbackResults();
  pdclient_->StoreChunk(chunk_name,
                        chunk_content,
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request,
                        maidsafe::SYSTEM_PACKET,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  boost::this_thread::sleep(boost::posix_time::seconds(2));

  std::string datastoredir("TestVault/ClientDatastore1");
  maidsafe::PDClient *newclient =  new maidsafe::PDClient(datastoredir,
                                                          63002,
                                                          kad_config_file_);
  testpdvault::PrepareCallbackResults();
  newclient->Join("",
                  boost::bind(&testpdvault::GeneralCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  testpdvault::PrepareCallbackResults();
  newclient->GetChunk(chunk_name,
                        boost::bind(&testpdvault::GetChunkCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_EQ(callback_content_, chunk_content);
  ASSERT_FALSE(callback_timed_out_);
  std::string hash = crypto_.Hash(callback_content_,
                                   "",
                                   maidsafe_crypto::STRING_STRING,
                                   false);
  ASSERT_EQ(chunk_name, hash);
  packethandler::GenericPacket gp;
  ASSERT_TRUE(gp.ParseFromString(callback_content_));
  testpdvault::PrepareCallbackResults();
  newclient->Leave(boost::bind(&testpdvault::GeneralCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  delete newclient;
}

TEST_F(TestPDVault, FUNC_MAID_StoreInvalidSystemPacket) {
  std::string chunk_name, chunk_content;
  maidsafe_crypto::RsaKeyPair keys;
  keys.GenerateKeys(packethandler::kRsaKeySize);
  testpdvault::CreateSystemPacket(keys.private_key(), &chunk_name,
    &chunk_content);
  std::string hex_chunk_name;
  base::encode_to_hex(chunk_name, hex_chunk_name);
  std::string signed_request =
      crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                    client_signed_public_key_+hex_chunk_name,
                                    "",
                                    maidsafe_crypto::STRING_STRING,
                                    true),
                       "",
                       client_private_key_,
                       maidsafe_crypto::STRING_STRING);
  testpdvault::PrepareCallbackResults();
  pdclient_->StoreChunk(chunk_name,
                        chunk_content,
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request,
                        maidsafe::SYSTEM_PACKET,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);

  testpdvault::PrepareCallbackResults();
  pdclient_->StoreChunk(chunk_name,
                        std::string("not a system packet"),
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request,
                        maidsafe::SYSTEM_PACKET,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
}

TEST_F(TestPDVault, FUNC_MAID_UpdatePDDirNotSigned) {
  std::string chunk_name = crypto_.Hash("abc", "",
                                        maidsafe_crypto::STRING_STRING, false);
  std::string chunk_content = base::RandomString(200);
  std::string chunk_name_enc;
  base::encode_to_hex(chunk_name, chunk_name_enc);
  std::string signed_request_ =
        crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                      client_signed_public_key_ +
                                      chunk_name_enc,
                                      "",
                                      maidsafe_crypto::STRING_STRING,
                                      true),
                         "",
                         client_private_key_,
                         maidsafe_crypto::STRING_STRING);
  testpdvault::PrepareCallbackResults();
  pdclient_->StoreChunk(chunk_name,
                        chunk_content,
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request_,
                        maidsafe::PDDIR_NOTSIGNED,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  boost::this_thread::sleep(boost::posix_time::seconds(2));
  // fail to store again on same key
  std::string new_chunk_content = base::RandomString(200);
//  testpdvault::PrepareCallbackResults();
//  pdclient_->StoreChunk(chunk_name,
//                        new_chunk_content,
//                        client_public_key_,
//                        client_signed_public_key_,
//                        signed_request_,
//                        maidsafe::PDDIR_NOTSIGNED,
//                        boost::bind(&testpdvault::StoreChunkCallback,
//                                    _1));
//  testpdvault::BluddyWaitFunction(120, recursive_mutex_client_.get());
//  ASSERT_FALSE(callback_succeeded_);
//  ASSERT_FALSE(callback_timed_out_);

  // Updating chunk
  testpdvault::PrepareCallbackResults();
  pdclient_->UpdateChunk(chunk_name,
                        new_chunk_content,
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request_,
                        maidsafe::PDDIR_NOTSIGNED,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  boost::this_thread::sleep(boost::posix_time::seconds(1));

  // loading chunk
  testpdvault::PrepareCallbackResults();
  pdclient_->GetChunk(chunk_name,
                      boost::bind(&testpdvault::GetChunkCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_EQ(callback_content_, new_chunk_content);
  ASSERT_FALSE(callback_timed_out_);
}

TEST_F(TestPDVault, FUNC_MAID_UpdateSystemPacket) {
  std::string chunk_name, chunk_content;
  testpdvault::CreateSystemPacket(client_private_key_, &chunk_name,
    &chunk_content);
  std::string hex_chunk_name;
  base::encode_to_hex(chunk_name, hex_chunk_name);
  std::string signed_request =
      crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                    client_signed_public_key_+hex_chunk_name,
                                    "",
                                    maidsafe_crypto::STRING_STRING,
                                    true),
                       "",
                       client_private_key_,
                       maidsafe_crypto::STRING_STRING);
  testpdvault::PrepareCallbackResults();
  pdclient_->StoreChunk(chunk_name,
                        chunk_content,
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request,
                        maidsafe::SYSTEM_PACKET,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  boost::this_thread::sleep(boost::posix_time::seconds(2));

  std::string new_chunk_content;
  std::string new_chunk_name;
  testpdvault::CreateSystemPacket(client_private_key_, &new_chunk_name,
    &new_chunk_content);
  ASSERT_NE(chunk_content, new_chunk_content);

  testpdvault::PrepareCallbackResults();
  pdclient_->UpdateChunk(chunk_name,
                        new_chunk_content,
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request,
                        maidsafe::SYSTEM_PACKET,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  boost::this_thread::sleep(boost::posix_time::seconds(1));

  testpdvault::PrepareCallbackResults();
  pdclient_->GetChunk(chunk_name,
                      boost::bind(&testpdvault::GetChunkCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_EQ(callback_content_, new_chunk_content);
  ASSERT_FALSE(callback_timed_out_);
}

TEST_F(TestPDVault, FUNC_MAID_UpdateInvalidSystemPacket) {
  std::string chunk_name, chunk_content;
  testpdvault::CreateSystemPacket(client_private_key_, &chunk_name,
    &chunk_content);
  std::string hex_chunk_name;
  base::encode_to_hex(chunk_name, hex_chunk_name);
  std::string signed_request =
      crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                    client_signed_public_key_+hex_chunk_name,
                                    "",
                                    maidsafe_crypto::STRING_STRING,
                                    true),
                       "",
                       client_private_key_,
                       maidsafe_crypto::STRING_STRING);
  testpdvault::PrepareCallbackResults();
  pdclient_->StoreChunk(chunk_name,
                        chunk_content,
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request,
                        maidsafe::SYSTEM_PACKET,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  boost::this_thread::sleep(boost::posix_time::seconds(2));

  testpdvault::PrepareCallbackResults();
  pdclient_->UpdateChunk(chunk_name,
                        std::string("this is not a system packet"),
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request,
                        maidsafe::SYSTEM_PACKET,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);

  // Udating different type
  testpdvault::PrepareCallbackResults();
  pdclient_->UpdateChunk(chunk_name,
                        std::string("this is not a system packet"),
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request,
                        maidsafe::PDDIR_NOTSIGNED,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);

  // System packet signed with different keys
  maidsafe_crypto::RsaKeyPair keys;
  keys.GenerateKeys(packethandler::kRsaKeySize);
  std::string new_chunk_content;
  std::string new_chunk_name;
  testpdvault::CreateSystemPacket(keys.private_key(), &new_chunk_name,
    &new_chunk_content);
  std::string sig_pubkey = crypto_.AsymSign(keys.public_key(), "",
     keys.private_key(), maidsafe_crypto::STRING_STRING);
  signed_request =
      crypto_.AsymSign(crypto_.Hash(keys.public_key() +
                                    client_signed_public_key_+hex_chunk_name,
                                    "",
                                    maidsafe_crypto::STRING_STRING,
                                    true),
                       "",
                       keys.private_key(),
                       maidsafe_crypto::STRING_STRING);
  testpdvault::PrepareCallbackResults();
  pdclient_->UpdateChunk(chunk_name,
                        std::string("this is not a system packet"),
                        keys.public_key(),
                        sig_pubkey,
                        signed_request,
                        maidsafe::SYSTEM_PACKET,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
}

TEST_F(TestPDVault, FUNC_MAID_AddGetMessages) {
  std::string chunk_name, chunk_content;
  testpdvault::CreateBufferPacket("publicuser", client_public_key_,
      client_private_key_,
    &chunk_name, &chunk_content);
  std::string chunk_name_enc;
  base::encode_to_hex(chunk_name, chunk_name_enc);
  std::string signed_request =
      crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                    client_signed_public_key_ + chunk_name_enc,
                                    "",
                                    maidsafe_crypto::STRING_STRING,
                                    true),
                       "",
                       client_private_key_,
                       maidsafe_crypto::STRING_STRING);
  testpdvault::PrepareCallbackResults();
  pdclient_->StoreChunk(chunk_name,
                        chunk_content,
                        client_public_key_,
                        client_signed_public_key_,
                        signed_request,
                        maidsafe::BUFFER_PACKET,
                        boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  boost::this_thread::sleep(boost::posix_time::seconds(3));

  // Updating bufferpacket info not being the owner
  maidsafe_crypto::RsaKeyPair keys;
  keys.GenerateKeys(packethandler::kRsaKeySize);
  std::string new_content;
  std::string expected_res;
  testpdvault::CreateMessage("test message", keys.public_key(),
    keys.private_key(), "public user2", packethandler::ADD_CONTACT_RQST,
    &new_content, &expected_res);
  std::string sig_pubkey = crypto_.AsymSign(keys.public_key(), "",
    keys.private_key(), maidsafe_crypto::STRING_STRING);

  signed_request =
      crypto_.AsymSign(crypto_.Hash(keys.public_key() +
                                    sig_pubkey + chunk_name_enc,
                                    "",
                                    maidsafe_crypto::STRING_STRING,
                                    true),
                       "",
                       keys.private_key(),
                       maidsafe_crypto::STRING_STRING);
  testpdvault::PrepareCallbackResults();
  pdclient_->UpdateChunk(chunk_name,
                         new_content,
                         keys.public_key(),
                         sig_pubkey,
                         signed_request,
                         maidsafe::BUFFER_PACKET_INFO,
                         boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);

  testpdvault::PrepareCallbackResults();
  pdclient_->UpdateChunk(chunk_name,
                         new_content,
                         keys.public_key(),
                         sig_pubkey,
                         signed_request,
                         maidsafe::BUFFER_PACKET_MESSAGE,
                         boost::bind(&testpdvault::StoreChunkCallback,
                                    _1));
  testpdvault::BluddyWaitFunction(120, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);

  // Getting the complete buffer packet
  testpdvault::PrepareCallbackResults();
  pdclient_->GetChunk(chunk_name,
                      boost::bind(&testpdvault::GetChunkCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  // verifying the buffer packet
  packethandler::BufferPacket rec_bp;
  ASSERT_TRUE(rec_bp.ParseFromString(callback_content_));
  ASSERT_EQ(1, rec_bp.messages_size());

  // Getting only the messages not owner
  testpdvault::PrepareCallbackResults();
  pdclient_->GetMessages(chunk_name, keys.public_key(), sig_pubkey,
                      boost::bind(&testpdvault::GetMessagesCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);

  // Getting messages
  testpdvault::PrepareCallbackResults();
  pdclient_->GetMessages(chunk_name,
                         client_public_key_,
                         client_signed_public_key_,
                         boost::bind(&testpdvault::GetMessagesCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  ASSERT_EQ(static_cast<unsigned int>(1), callback_msgs.size());
  ASSERT_EQ(expected_res, callback_msgs.front());
  // Deleting the messages not owner
  testpdvault::PrepareCallbackResults();
  pdclient_->DeleteChunk(chunk_name,
                         keys.public_key(),
                         sig_pubkey,
                         signed_request,
                         maidsafe::BUFFER_PACKET_MESSAGE,
                         boost::bind(&testpdvault::DeleteCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_FALSE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);

  // Deleting messages
  signed_request =
      crypto_.AsymSign(crypto_.Hash(client_public_key_ +
                                    client_signed_public_key_ + chunk_name_enc,
                                    "",
                                    maidsafe_crypto::STRING_STRING,
                                    true),
                       "",
                       client_private_key_,
                       maidsafe_crypto::STRING_STRING);
  testpdvault::PrepareCallbackResults();
  pdclient_->DeleteChunk(chunk_name,
                         client_public_key_,
                         client_signed_public_key_,
                         signed_request,
                         maidsafe::BUFFER_PACKET_MESSAGE,
                         boost::bind(&testpdvault::DeleteCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);

  // Getting messages again
  testpdvault::PrepareCallbackResults();
  pdclient_->GetMessages(chunk_name,
                         client_public_key_,
                         client_signed_public_key_,
                         boost::bind(&testpdvault::GetMessagesCallback, _1));
  testpdvault::BluddyWaitFunction(60, &mutex_);
  ASSERT_TRUE(callback_succeeded_);
  ASSERT_FALSE(callback_timed_out_);
  ASSERT_EQ(static_cast<unsigned int>(0), callback_msgs.size());
}

TEST_F(TestPDVault, DISABLED_FUNC_MAID_SwapChunk) {
}

TEST_F(TestPDVault, DISABLED_FUNC_MAID_VaultValidateChunk) {
  // check pre-loaded chunks are not corrupted
}

TEST_F(TestPDVault, DISABLED_FUNC_MAID_VaultRepublishChunkRef) {
}

}  // namespace maidsafe_vault

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::AddGlobalTestEnvironment(
      new localvaults::Env(maidsafe_vault::kNetworkSize_,
                           maidsafe_vault::kTestK_,
                           &maidsafe_vault::pdvaults_));
  return RUN_ALL_TESTS();
}
